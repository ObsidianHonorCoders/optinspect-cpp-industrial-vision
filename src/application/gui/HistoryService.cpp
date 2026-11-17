#include "HistoryService.hpp"

#include "ImageLoader.hpp"

#include "common/Timing.hpp"
#include "database/DatabaseConnection.hpp"
#include "database/DatabaseError.hpp"
#include "database/DatabaseInitializer.hpp"
#include "database/DefectRepository.hpp"
#include "database/InspectionImageRepository.hpp"
#include "database/InspectionRepository.hpp"
#include "database/ProductRepository.hpp"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace optinspect::gui {

using namespace database;

namespace {

DatabaseConnection openReadOnly(const std::string& path) {
    if (path.empty()) throw DatabaseError("No database path selected.");
    DatabaseConnection connection = DatabaseConnection::open(path, OpenMode::ReadOnly);
    DatabaseInitializer::requireCurrentSchema(connection);
    return connection;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Stage timings from the JSON report written next to the inspection (if the
/// file still exists and is readable). Absence is normal and not an error.
std::vector<StageTimingRow> readStageTimings(const std::string& report_path) {
    std::vector<StageTimingRow> rows;
    if (report_path.empty()) return rows;
    try {
        std::ifstream in(report_path);
        if (!in) return rows;
        const nlohmann::json report = nlohmann::json::parse(in);
        if (!report.contains("stage_timings_ms")) return rows;
        for (const auto& t : report["stage_timings_ms"]) {
            rows.push_back({t.value("stage", std::string()), t.value("milliseconds", 0.0)});
        }
    } catch (const std::exception&) {
        rows.clear();
    }
    return rows;
}

}  // namespace

HistoryResult queryHistory(const HistoryQuery& query) {
    Stopwatch timer;
    DatabaseConnection connection = openReadOnly(query.database_path);
    InspectionRepository inspections(connection);
    ProductRepository products(connection);

    HistoryResult result;
    result.matching_count = inspections.count(query.filter);
    result.inspections = inspections.list(query.filter);
    result.products = products.list();

    if (!query.id_search.empty()) {
        const std::string needle = lower(query.id_search);
        result.inspections.erase(
            std::remove_if(result.inspections.begin(), result.inspections.end(),
                           [&](const InspectionRecord& r) {
                               return !r.external_id ||
                                      lower(*r.external_id).find(needle) == std::string::npos;
                           }),
            result.inspections.end());
    }
    result.query_ms = timer.elapsedMilliseconds();
    return result;
}

AnalyticsSnapshot loadAnalytics(const AnalyticsQuery& query) {
    Stopwatch timer;
    DatabaseConnection connection = openReadOnly(query.database_path);
    QualityAnalyticsService analytics(connection);

    AnalyticsSnapshot s;
    s.summary = analytics.summary(query.filter);
    s.defects = analytics.defectStatistics(query.filter);
    s.products = analytics.productQuality(query.filter);
    s.trend = analytics.timeSeries(query.granularity, query.filter);
    s.processing = analytics.processingTime(query.filter);
    s.processing_by_status = analytics.processingTimeByStatus(query.filter);
    s.processing_by_product = analytics.processingTimeByProduct(query.filter);
    s.query_ms = timer.elapsedMilliseconds();
    return s;
}

std::vector<DefectRow> toDefectRows(const std::vector<DefectRecord>& records) {
    std::vector<DefectRow> rows;
    rows.reserve(records.size());
    int index = 1;
    for (const DefectRecord& d : records) {
        DefectRow r;
        r.index = index++;
        r.x = d.x;
        r.y = d.y;
        r.width = d.width;
        r.height = d.height;
        r.area_px = d.area;
        r.perimeter_px = d.perimeter;
        r.centroid_x = d.centroid_x;
        r.centroid_y = d.centroid_y;
        r.aspect_ratio = d.aspect_ratio;
        r.confidence = d.confidence;
        r.review_note = d.review_note;
        rows.push_back(std::move(r));
    }
    return rows;
}

InspectionView loadInspectionView(const std::string& database_path, std::int64_t inspection_id,
                                  const std::unordered_set<std::string>& cached_keys) {
    InspectionRecord record;
    std::optional<Product> product;
    std::vector<DefectRecord> defects;
    std::vector<InspectionImage> images;
    {
        DatabaseConnection connection = openReadOnly(database_path);
        InspectionRepository inspections(connection);
        const std::optional<InspectionRecord> found = inspections.getById(inspection_id);
        if (!found) throw DatabaseError("Inspection " + std::to_string(inspection_id) + " does not exist.");
        record = *found;
        if (record.product_id) product = ProductRepository(connection).getById(*record.product_id);
        defects = DefectRepository(connection).listByInspection(inspection_id);
        images = InspectionImageRepository(connection).listByInspection(inspection_id);
    }

    InspectionView view;
    view.source = ResultSource::History;
    view.db_id = record.id;
    view.external_id = record.external_id.value_or("(no external id)");
    view.decision = toString(record.status);
    view.decision_reason = record.decision_reason.value_or("");
    if (record.alignment_status) {
        view.alignment_succeeded = (*record.alignment_status == AlignmentStatus::SUCCEEDED);
    }
    view.alignment_confidence = record.alignment_confidence;
    view.product_id = record.product_id;
    if (product) view.product_label = product->code + " - " + product->name;
    view.serial_number = record.serial_number;
    view.timestamp = record.inspection_timestamp;
    view.processing_time_ms = record.processing_time_ms;
    view.report_path = record.report_path.value_or("");
    view.defects = toDefectRows(defects);
    view.stages = readStageTimings(view.report_path);

    const auto slotFor = [&](ImageType type, ImageSlot& slot, const char* label) {
        for (const InspectionImage& img : images) {
            if (img.type != type) continue;
            slot = loadImageSlot(img.file_path, cached_keys);
            if (!slot.error.empty()) view.warnings.push_back(std::string(label) + " image: " + slot.error);
            return;
        }
        slot.error = "No reference to this image is stored for the inspection.";
        if (type != ImageType::DIFFERENCE) view.warnings.push_back(std::string(label) + " image: " + slot.error);
    };
    slotFor(ImageType::REFERENCE, view.images.reference, "Reference");
    slotFor(ImageType::INSPECTED, view.images.inspected, "Inspected");
    slotFor(ImageType::ANNOTATED, view.images.annotated, "Annotated");
    slotFor(ImageType::DIFFERENCE, view.images.difference, "Difference");
    return view;
}

}  // namespace optinspect::gui
