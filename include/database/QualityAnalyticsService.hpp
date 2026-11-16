#pragma once

#include "database/DatabaseConnection.hpp"
#include "database/Models.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace optinspect::database {

// All analytics are computed by SQL over the stored inspection data and can be
// narrowed with an InspectionFilter (product, serial number, status, dates).
// `limit`/`offset` of the filter are ignored.
//
// Rates are fractions in [0, 1] whose denominator is the number of inspections
// in scope (PASS + FAIL + REVIEW); they are 0 when there are no inspections.
//
// These are production-quality indicators over detector OUTPUT. They are not
// detector accuracy metrics: without ground-truth annotations nothing here can
// say whether a detected region is a true or false positive.

struct InspectionSummary {
    int total = 0;
    int pass = 0;
    int fail = 0;
    int review = 0;
    double pass_rate = 0.0;
    double fail_rate = 0.0;
    double review_rate = 0.0;
};

struct ConfidenceBandCount {
    /// LOW (< 0.33), MEDIUM (< 0.66), HIGH (>= 0.66) or UNSCORED, derived from
    /// the Phase 1 rule-based region score. A reporting bucket, not a defect class.
    std::string band;
    int count = 0;
};

struct DefectStatistics {
    int inspections = 0;  ///< Inspections in scope, including those with zero defects.
    int total_defects = 0;
    double avg_defects_per_inspection = 0.0;
    int max_defects_in_inspection = 0;
    std::optional<double> avg_defect_area_px;  ///< Empty when there are no defects.
    std::vector<ConfidenceBandCount> by_confidence_band;
};

struct ProductQuality {
    std::optional<std::int64_t> product_id;  ///< Empty for inspections without a product.
    std::string product_code;                ///< "(unassigned)" when product_id is empty.
    std::string product_name;
    int total = 0;
    int failed = 0;
    int review = 0;
    double failure_rate = 0.0;  ///< failed / total.
    std::optional<double> avg_processing_time_ms;
};

enum class TimeGranularity { Day, Week, Month };

struct PeriodStatistics {
    std::string period;  ///< "YYYY-MM-DD", "YYYY-Www" (week of year, Monday start) or "YYYY-MM".
    int total = 0;
    int pass = 0;
    int fail = 0;
    int review = 0;
    double fail_rate = 0.0;
    int total_defects = 0;
    double avg_defects_per_inspection = 0.0;
    int cumulative_inspections = 0;
    /// Change in average defects per inspection versus the previous period
    /// (window function LAG); empty for the first period.
    std::optional<double> defects_per_inspection_change;
};

struct ProcessingTimeStats {
    int samples = 0;  ///< Inspections that have a recorded processing time.
    std::optional<double> avg_ms;
    std::optional<double> min_ms;
    std::optional<double> max_ms;
};

struct GroupedProcessingTime {
    std::string group;  ///< Status name or product code.
    ProcessingTimeStats stats;
};

class QualityAnalyticsService {
public:
    explicit QualityAnalyticsService(DatabaseConnection& connection) : db_(connection) {}

    InspectionSummary summary(const InspectionFilter& filter = {});
    DefectStatistics defectStatistics(const InspectionFilter& filter = {});

    /// One row per product (and one for unassigned inspections), most failing
    /// first. Products with fewer than `min_inspections` inspections are omitted (HAVING).
    std::vector<ProductQuality> productQuality(const InspectionFilter& filter = {},
                                               int min_inspections = 1);

    std::vector<PeriodStatistics> timeSeries(TimeGranularity granularity,
                                             const InspectionFilter& filter = {});

    ProcessingTimeStats processingTime(const InspectionFilter& filter = {});
    std::vector<GroupedProcessingTime> processingTimeByStatus(const InspectionFilter& filter = {});
    std::vector<GroupedProcessingTime> processingTimeByProduct(const InspectionFilter& filter = {});

private:
    DatabaseConnection& db_;
};

}  // namespace optinspect::database
