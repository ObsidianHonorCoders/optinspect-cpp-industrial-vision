#include "InspectionRecorder.hpp"

#include "ReportWriter.hpp"
#include "common/Errors.hpp"
#include "database/DatabaseError.hpp"
#include "database/DatabaseInitializer.hpp"
#include "database/InspectionRepository.hpp"
#include "database/ProductRepository.hpp"
#include "database/TimeUtil.hpp"
#include "third_party/nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace optinspect {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

database::InspectionStatus toDatabaseStatus(InspectionDecision decision) {
    switch (decision) {
        case InspectionDecision::PASS: return database::InspectionStatus::PASS;
        case InspectionDecision::FAIL: return database::InspectionStatus::FAIL;
        case InspectionDecision::REVIEW: return database::InspectionStatus::REVIEW;
    }
    return database::InspectionStatus::REVIEW;
}

void addImage(database::InspectionPersistenceRequest& request, database::ImageType type,
              const std::string& path, int width, int height) {
    if (path.empty()) return;
    database::InspectionImage image;
    image.type = type;
    image.file_path = path;
    if (width > 0) image.width = width;
    if (height > 0) image.height = height;
    request.images.push_back(image);
}

database::DefectRecord toDefectRecord(const DefectMeasurement& m) {
    database::DefectRecord d;
    d.x = m.bounding_box.x;
    d.y = m.bounding_box.y;
    d.width = m.bounding_box.width;
    d.height = m.bounding_box.height;
    d.area = m.area_px;
    d.perimeter = m.perimeter_px;
    d.centroid_x = m.centroid.x;
    d.centroid_y = m.centroid.y;
    d.aspect_ratio = m.aspect_ratio;
    d.confidence = m.confidence;
    return d;
}

std::string absolutePath(const fs::path& p) { return fs::absolute(p).lexically_normal().string(); }

template <typename T>
T requireField(const json& j, const char* key) {
    if (!j.contains(key)) {
        throw OptInspectError(std::string("Report is missing required field '") + key + "'.");
    }
    return j.at(key).get<T>();
}

}  // namespace

database::InspectionPersistenceRequest buildPersistenceRequest(
    const InspectionResult& result, const InspectionConfig& config,
    const InspectionArtifacts& artifacts, const PersistenceOptions& options,
    const std::string& timestamp_utc) {
    database::InspectionPersistenceRequest request;

    database::InspectionRecord& inspection = request.inspection;
    inspection.external_id = artifacts.external_id;
    inspection.product_id = options.product_id;
    inspection.serial_number = options.serial_number;
    inspection.notes = options.notes;
    inspection.inspection_timestamp = timestamp_utc;
    inspection.status = toDatabaseStatus(result.decision);
    inspection.decision_reason = result.decision_reason;
    inspection.alignment_status = result.alignment_succeeded ? database::AlignmentStatus::SUCCEEDED
                                                              : database::AlignmentStatus::FAILED;
    inspection.alignment_confidence = result.alignment_confidence;
    if (!artifacts.report_path.empty()) inspection.report_path = artifacts.report_path;
    inspection.processing_time_ms = artifacts.processing_time_ms;
    inspection.configuration_snapshot = ReportWriter::configurationJson(config);

    for (const DefectMeasurement& defect : result.defects) {
        request.defects.push_back(toDefectRecord(defect));
    }

    addImage(request, database::ImageType::REFERENCE, artifacts.reference_path,
             artifacts.reference_size.width, artifacts.reference_size.height);
    addImage(request, database::ImageType::INSPECTED, artifacts.inspected_path,
             artifacts.inspected_size.width, artifacts.inspected_size.height);
    addImage(request, database::ImageType::ANNOTATED, artifacts.annotated_path,
             artifacts.annotated_size.width, artifacts.annotated_size.height);
    addImage(request, database::ImageType::DIFFERENCE, artifacts.difference_path,
             artifacts.difference_size.width, artifacts.difference_size.height);
    return request;
}

database::InspectionPersistenceRequest buildRequestFromReport(const std::string& report_path,
                                                              const PersistenceOptions& options) {
    std::ifstream file(report_path);
    if (!file.is_open()) {
        throw OptInspectError("Cannot open report file: " + report_path);
    }

    json report;
    try {
        file >> report;
    } catch (const json::parse_error& e) {
        throw OptInspectError("Malformed JSON in report '" + report_path + "': " + e.what());
    }

    database::InspectionPersistenceRequest request;
    try {
        const std::string id = requireField<std::string>(report, "inspection_id");
        const json& alignment = report.at("alignment");

        database::InspectionRecord& inspection = request.inspection;
        inspection.external_id = id;
        inspection.product_id = options.product_id;
        inspection.serial_number = options.serial_number;
        inspection.notes = options.notes;
        inspection.status = database::parseInspectionStatus(requireField<std::string>(report, "decision"));
        inspection.decision_reason = requireField<std::string>(report, "decision_reason");
        inspection.alignment_status = alignment.at("succeeded").get<bool>()
                                          ? database::AlignmentStatus::SUCCEEDED
                                          : database::AlignmentStatus::FAILED;
        inspection.alignment_confidence = alignment.at("confidence").get<double>();
        inspection.report_path = absolutePath(report_path);
        // The report stores the sum of the stage timings, not the wall-clock total.
        inspection.processing_time_ms = requireField<double>(report, "total_processing_time_ms");
        if (report.contains("configuration")) {
            inspection.configuration_snapshot = report.at("configuration").dump();
        }

        // The id starts with the local time of the original run (YYYYMMDD-HHMMSS).
        inspection.inspection_timestamp = database::utcTimestampFromLocalStamp(id);
        if (inspection.inspection_timestamp.empty()) {
            inspection.inspection_timestamp = database::nowUtcTimestamp();
        }

        for (const json& d : report.at("defects")) {
            database::DefectRecord defect;
            const json& box = d.at("bounding_box");
            defect.x = box.at("x").get<int>();
            defect.y = box.at("y").get<int>();
            defect.width = box.at("width").get<int>();
            defect.height = box.at("height").get<int>();
            defect.area = d.at("area_px").get<double>();
            defect.perimeter = d.at("perimeter_px").get<double>();
            defect.centroid_x = d.at("centroid").at("x").get<double>();
            defect.centroid_y = d.at("centroid").at("y").get<double>();
            defect.aspect_ratio = d.at("aspect_ratio").get<double>();
            defect.confidence = d.at("confidence").get<double>();
            request.defects.push_back(defect);
        }

        const fs::path report_dir = fs::path(report_path).parent_path();
        const std::string suffix = "_report.json";
        const std::string file_name = fs::path(report_path).filename().string();
        const std::string stem =
            file_name.size() > suffix.size() &&
                    file_name.compare(file_name.size() - suffix.size(), suffix.size(), suffix) == 0
                ? file_name.substr(0, file_name.size() - suffix.size())
                : id;

        addImage(request, database::ImageType::REFERENCE,
                 absolutePath(requireField<std::string>(report, "reference_image")),
                 report.at("reference_dimensions").at("width").get<int>(),
                 report.at("reference_dimensions").at("height").get<int>());
        addImage(request, database::ImageType::INSPECTED,
                 absolutePath(requireField<std::string>(report, "inspected_image")),
                 report.at("inspected_dimensions").at("width").get<int>(),
                 report.at("inspected_dimensions").at("height").get<int>());

        const fs::path annotated = report_dir / (stem + "_annotated.png");
        if (fs::exists(annotated)) {
            addImage(request, database::ImageType::ANNOTATED, absolutePath(annotated), 0, 0);
        }
        const fs::path difference = report_dir / (stem + "_difference.png");
        if (fs::exists(difference)) {
            addImage(request, database::ImageType::DIFFERENCE, absolutePath(difference), 0, 0);
        }
    } catch (const json::exception& e) {
        throw OptInspectError("Report '" + report_path + "' does not have the expected structure: " +
                              e.what());
    } catch (const std::invalid_argument& e) {
        throw OptInspectError("Report '" + report_path + "' is invalid: " + e.what());
    }
    return request;
}

void ensureParentDirectory(const std::string& database_path) {
    const fs::path parent = fs::path(database_path).parent_path();
    if (!parent.empty()) fs::create_directories(parent);
}

namespace {

database::DatabaseConnection openRecorderConnection(const std::string& path, bool auto_initialize) {
    if (auto_initialize) {
        ensureParentDirectory(path);
        database::DatabaseConnection connection = database::DatabaseConnection::open(path);
        database::DatabaseInitializer::initialize(connection);
        return connection;
    }
    if (!fs::exists(path)) {
        throw database::DatabaseError("Database file not found: " + path +
                                      ". Run: optinspect db init --database " + path);
    }
    database::DatabaseConnection connection =
        database::DatabaseConnection::open(path, database::OpenMode::ReadWrite);
    database::DatabaseInitializer::requireCurrentSchema(connection);
    return connection;
}

}  // namespace

InspectionRecorder::InspectionRecorder(const std::string& database_path, bool auto_initialize)
    : connection_(openRecorderConnection(database_path, auto_initialize)) {}

std::int64_t InspectionRecorder::record(const database::InspectionPersistenceRequest& request) {
    database::InspectionPersistenceService service(connection_);
    return service.persist(request);
}

bool InspectionRecorder::productExists(std::int64_t product_id) {
    return database::ProductRepository(connection_).getById(product_id).has_value();
}

std::optional<std::int64_t> InspectionRecorder::findByExternalId(const std::string& external_id) {
    const auto record = database::InspectionRepository(connection_).getByExternalId(external_id);
    if (!record) return std::nullopt;
    return record->id;
}

}  // namespace optinspect
