#pragma once

#include "common/Config.hpp"
#include "database/DatabaseConnection.hpp"
#include "database/InspectionPersistenceService.hpp"
#include "detection/InspectionResult.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace optinspect {

/// Files produced by one inspection run, plus the figures measured around the
/// pipeline. Paths should be absolute so the stored references stay valid
/// whatever directory the application is later started from.
struct InspectionArtifacts {
    std::string external_id;  ///< Phase 1 inspection id (names the output files).
    std::string reference_path;
    std::string inspected_path;
    std::string annotated_path;
    std::string difference_path;  ///< Empty when no difference image was produced.
    std::string report_path;
    cv::Size reference_size;
    cv::Size inspected_size;
    cv::Size annotated_size;
    cv::Size difference_size;
    double processing_time_ms = 0.0;
};

/// Optional business metadata attached to a persisted inspection.
struct PersistenceOptions {
    std::optional<std::int64_t> product_id;
    std::optional<std::string> serial_number;
    std::optional<std::string> notes;
};

/// Maps a finished Phase 1 inspection onto the database layer's plain records.
/// This is the only place where engine types (InspectionResult, DefectMeasurement)
/// meet database types, which keeps the database module free of OpenCV.
database::InspectionPersistenceRequest buildPersistenceRequest(
    const InspectionResult& result, const InspectionConfig& config,
    const InspectionArtifacts& artifacts, const PersistenceOptions& options,
    const std::string& timestamp_utc);

/// Rebuilds a persistence request from a `<id>_report.json` written by
/// ReportWriter. Used to retry persistence after a database failure without
/// re-running the inspection. The annotated and difference images are linked
/// when files following the Phase 1 naming scheme sit next to the report.
/// Throws OptInspectError if the report is missing or malformed.
database::InspectionPersistenceRequest buildRequestFromReport(const std::string& report_path,
                                                              const PersistenceOptions& options);

/// Opens (and optionally initializes) the database used for persistence and
/// stores inspections in it. Constructing it may throw database::DatabaseError.
class InspectionRecorder {
public:
    InspectionRecorder(const std::string& database_path, bool auto_initialize);

    /// Persists the request atomically and returns the new inspection id.
    std::int64_t record(const database::InspectionPersistenceRequest& request);

    bool productExists(std::int64_t product_id);

    /// Id of the stored inspection that was recorded under `external_id`, if any.
    std::optional<std::int64_t> findByExternalId(const std::string& external_id);
    const std::string& databasePath() const { return connection_.path(); }

private:
    database::DatabaseConnection connection_;
};

/// Creates the parent directory of a database file if it does not exist yet.
void ensureParentDirectory(const std::string& database_path);

}  // namespace optinspect
