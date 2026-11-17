#pragma once

#include "GuiTypes.hpp"

#include "database/Models.hpp"
#include "database/QualityAnalyticsService.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace optinspect::gui {

/// Thin, UI-free layer between the GUI and the Phase 2 repositories. It adds no
/// SQL of its own: every query goes through InspectionRepository,
/// ProductRepository, DefectRepository, InspectionImageRepository or
/// QualityAnalyticsService. Each call opens its OWN read-only connection and
/// closes it before returning, so the functions can run on a worker thread
/// without sharing a SQLite connection with any other thread, and they can
/// never modify the history. They throw database::DatabaseError (or another
/// std::exception) on failure.

struct HistoryQuery {
    std::string database_path;
    database::InspectionFilter filter;  ///< status / product / serial / date range / limit.
    /// Case-insensitive substring of the inspection identifier. Applied to the
    /// rows returned by the repository query (the repository has no text search).
    std::string id_search;
};

struct HistoryResult {
    std::vector<database::InspectionRecord> inspections;
    std::vector<database::Product> products;  ///< All products, for the product filter.
    int matching_count = 0;                   ///< Rows matching the filter before id_search/limit.
    double query_ms = 0.0;
};

HistoryResult queryHistory(const HistoryQuery& query);

struct AnalyticsQuery {
    std::string database_path;
    database::InspectionFilter filter;
    database::TimeGranularity granularity = database::TimeGranularity::Day;
};

struct AnalyticsSnapshot {
    database::InspectionSummary summary;
    database::DefectStatistics defects;
    std::vector<database::ProductQuality> products;
    std::vector<database::PeriodStatistics> trend;
    database::ProcessingTimeStats processing;
    std::vector<database::GroupedProcessingTime> processing_by_status;
    std::vector<database::GroupedProcessingTime> processing_by_product;
    double query_ms = 0.0;
};

AnalyticsSnapshot loadAnalytics(const AnalyticsQuery& query);

/// Retrieves one stored inspection with its defects and image references, then
/// loads the referenced image files (on the calling thread). Missing or
/// unreadable files are reported through the slots and `warnings`; the database
/// record is never altered and no substitute image is used. Throws when the
/// inspection does not exist.
InspectionView loadInspectionView(const std::string& database_path, std::int64_t inspection_id,
                                  const std::unordered_set<std::string>& cached_keys);

/// Converts stored defect rows to display rows (1-based indices in stored order).
std::vector<DefectRow> toDefectRows(const std::vector<database::DefectRecord>& records);

}  // namespace optinspect::gui
