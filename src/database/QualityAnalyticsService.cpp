#include "database/QualityAnalyticsService.hpp"

#include "InspectionQuery.hpp"
#include "database/Statement.hpp"

namespace optinspect::database {

namespace {

// SUM(boolean) yields NULL on an empty set; COALESCE keeps counts at 0.
const char* const kStatusCounts =
    "COALESCE(SUM(i.status = 'PASS'), 0), COALESCE(SUM(i.status = 'FAIL'), 0), "
    "COALESCE(SUM(i.status = 'REVIEW'), 0)";

ProcessingTimeStats readTimeStats(const Statement& stmt, int first_column) {
    ProcessingTimeStats stats;
    stats.samples = stmt.columnInt(first_column);
    stats.avg_ms = stmt.columnOptionalDouble(first_column + 1);
    stats.min_ms = stmt.columnOptionalDouble(first_column + 2);
    stats.max_ms = stmt.columnOptionalDouble(first_column + 3);
    return stats;
}

const char* periodExpression(TimeGranularity granularity) {
    switch (granularity) {
        case TimeGranularity::Day: return "substr(i.inspection_timestamp, 1, 10)";
        case TimeGranularity::Week: return "strftime('%Y-W%W', i.inspection_timestamp)";
        case TimeGranularity::Month: return "substr(i.inspection_timestamp, 1, 7)";
    }
    return "substr(i.inspection_timestamp, 1, 10)";
}

}  // namespace

InspectionSummary QualityAnalyticsService::summary(const InspectionFilter& filter) {
    Statement stmt(
        db_,
        std::string("SELECT COUNT(*), ") + kStatusCounts + ", "
        "CASE WHEN COUNT(*) = 0 THEN 0.0 ELSE 1.0 * SUM(i.status = 'PASS') / COUNT(*) END, "
        "CASE WHEN COUNT(*) = 0 THEN 0.0 ELSE 1.0 * SUM(i.status = 'FAIL') / COUNT(*) END, "
        "CASE WHEN COUNT(*) = 0 THEN 0.0 ELSE 1.0 * SUM(i.status = 'REVIEW') / COUNT(*) END "
        "FROM inspections i" + detail::inspectionWhereClause(filter));
    detail::bindInspectionFilter(stmt, filter);

    InspectionSummary s;
    if (stmt.step()) {
        s.total = stmt.columnInt(0);
        s.pass = stmt.columnInt(1);
        s.fail = stmt.columnInt(2);
        s.review = stmt.columnInt(3);
        s.pass_rate = stmt.columnDouble(4);
        s.fail_rate = stmt.columnDouble(5);
        s.review_rate = stmt.columnDouble(6);
    }
    return s;
}

DefectStatistics QualityAnalyticsService::defectStatistics(const InspectionFilter& filter) {
    const std::string where = detail::inspectionWhereClause(filter);
    DefectStatistics result;

    {
        // The LEFT JOIN keeps inspections with zero defects in the averages.
        Statement stmt(db_,
                       "WITH scoped AS (SELECT i.inspection_id FROM inspections i" + where + "), "
                       "per_inspection AS ("
                       "  SELECT s.inspection_id, COUNT(d.defect_id) AS defect_count "
                       "  FROM scoped s LEFT JOIN defects d ON d.inspection_id = s.inspection_id "
                       "  GROUP BY s.inspection_id) "
                       "SELECT COUNT(*), COALESCE(SUM(defect_count), 0), "
                       "COALESCE(AVG(defect_count), 0.0), COALESCE(MAX(defect_count), 0) "
                       "FROM per_inspection");
        detail::bindInspectionFilter(stmt, filter);
        if (stmt.step()) {
            result.inspections = stmt.columnInt(0);
            result.total_defects = stmt.columnInt(1);
            result.avg_defects_per_inspection = stmt.columnDouble(2);
            result.max_defects_in_inspection = stmt.columnInt(3);
        }
    }
    {
        Statement stmt(db_,
                       "SELECT AVG(d.area) FROM defects d "
                       "JOIN inspections i ON i.inspection_id = d.inspection_id" + where);
        detail::bindInspectionFilter(stmt, filter);
        if (stmt.step()) result.avg_defect_area_px = stmt.columnOptionalDouble(0);
    }
    {
        Statement stmt(db_,
                       "SELECT CASE WHEN d.confidence IS NULL THEN 'UNSCORED' "
                       "WHEN d.confidence < 0.33 THEN 'LOW' "
                       "WHEN d.confidence < 0.66 THEN 'MEDIUM' ELSE 'HIGH' END AS band, COUNT(*) "
                       "FROM defects d JOIN inspections i ON i.inspection_id = d.inspection_id" +
                           where +
                           " GROUP BY band ORDER BY MIN(COALESCE(d.confidence, -1))");
        detail::bindInspectionFilter(stmt, filter);
        while (stmt.step()) {
            result.by_confidence_band.push_back({stmt.columnText(0), stmt.columnInt(1)});
        }
    }
    return result;
}

std::vector<ProductQuality> QualityAnalyticsService::productQuality(const InspectionFilter& filter,
                                                                    int min_inspections) {
    Statement stmt(
        db_,
        "SELECT i.product_id, COALESCE(p.product_code, '(unassigned)'), COALESCE(p.product_name, ''), "
        "COUNT(*) AS total, COALESCE(SUM(i.status = 'FAIL'), 0) AS failed, "
        "COALESCE(SUM(i.status = 'REVIEW'), 0) AS review, "
        "1.0 * SUM(i.status = 'FAIL') / COUNT(*) AS failure_rate, AVG(i.processing_time_ms) "
        "FROM inspections i LEFT JOIN products p ON p.product_id = i.product_id" +
            detail::inspectionWhereClause(filter) +
            " GROUP BY i.product_id HAVING COUNT(*) >= :min_inspections "
            "ORDER BY failure_rate DESC, total DESC, 2");
    detail::bindInspectionFilter(stmt, filter);
    stmt.bind(":min_inspections", min_inspections);

    std::vector<ProductQuality> rows;
    while (stmt.step()) {
        ProductQuality q;
        q.product_id = stmt.columnOptionalInt64(0);
        q.product_code = stmt.columnText(1);
        q.product_name = stmt.columnText(2);
        q.total = stmt.columnInt(3);
        q.failed = stmt.columnInt(4);
        q.review = stmt.columnInt(5);
        q.failure_rate = stmt.columnDouble(6);
        q.avg_processing_time_ms = stmt.columnOptionalDouble(7);
        rows.push_back(q);
    }
    return rows;
}

std::vector<PeriodStatistics> QualityAnalyticsService::timeSeries(TimeGranularity granularity,
                                                                  const InspectionFilter& filter) {
    // The CTE aggregates per period; the outer query adds window functions
    // over the ordered periods (running total and period-over-period change).
    Statement stmt(
        db_,
        std::string("WITH per_period AS ("
                    "  SELECT ") + periodExpression(granularity) + " AS period, COUNT(*) AS total, "
            "  COALESCE(SUM(i.status = 'PASS'), 0) AS pass_count, "
            "  COALESCE(SUM(i.status = 'FAIL'), 0) AS fail_count, "
            "  COALESCE(SUM(i.status = 'REVIEW'), 0) AS review_count, "
            "  COALESCE(SUM(i.detected_defect_count), 0) AS defects "
            "  FROM inspections i" + detail::inspectionWhereClause(filter) +
            "  GROUP BY period) "
            "SELECT period, total, pass_count, fail_count, review_count, "
            "  1.0 * fail_count / total, defects, 1.0 * defects / total, "
            "  SUM(total) OVER (ORDER BY period), "
            "  1.0 * defects / total - LAG(1.0 * defects / total) OVER (ORDER BY period) "
            "FROM per_period ORDER BY period");
    detail::bindInspectionFilter(stmt, filter);

    std::vector<PeriodStatistics> rows;
    while (stmt.step()) {
        PeriodStatistics p;
        p.period = stmt.columnText(0);
        p.total = stmt.columnInt(1);
        p.pass = stmt.columnInt(2);
        p.fail = stmt.columnInt(3);
        p.review = stmt.columnInt(4);
        p.fail_rate = stmt.columnDouble(5);
        p.total_defects = stmt.columnInt(6);
        p.avg_defects_per_inspection = stmt.columnDouble(7);
        p.cumulative_inspections = stmt.columnInt(8);
        p.defects_per_inspection_change = stmt.columnOptionalDouble(9);
        rows.push_back(p);
    }
    return rows;
}

ProcessingTimeStats QualityAnalyticsService::processingTime(const InspectionFilter& filter) {
    // COUNT(column) ignores NULLs, so inspections without a recorded time do
    // not distort the average, minimum or maximum.
    Statement stmt(db_,
                   "SELECT COUNT(i.processing_time_ms), AVG(i.processing_time_ms), "
                   "MIN(i.processing_time_ms), MAX(i.processing_time_ms) FROM inspections i" +
                       detail::inspectionWhereClause(filter));
    detail::bindInspectionFilter(stmt, filter);
    ProcessingTimeStats stats;
    if (stmt.step()) stats = readTimeStats(stmt, 0);
    return stats;
}

std::vector<GroupedProcessingTime> QualityAnalyticsService::processingTimeByStatus(
    const InspectionFilter& filter) {
    Statement stmt(db_,
                   "SELECT i.status, COUNT(i.processing_time_ms), AVG(i.processing_time_ms), "
                   "MIN(i.processing_time_ms), MAX(i.processing_time_ms) FROM inspections i" +
                       detail::inspectionWhereClause(filter) + " GROUP BY i.status ORDER BY i.status");
    detail::bindInspectionFilter(stmt, filter);
    std::vector<GroupedProcessingTime> rows;
    while (stmt.step()) rows.push_back({stmt.columnText(0), readTimeStats(stmt, 1)});
    return rows;
}

std::vector<GroupedProcessingTime> QualityAnalyticsService::processingTimeByProduct(
    const InspectionFilter& filter) {
    Statement stmt(db_,
                   "SELECT COALESCE(p.product_code, '(unassigned)'), COUNT(i.processing_time_ms), "
                   "AVG(i.processing_time_ms), MIN(i.processing_time_ms), MAX(i.processing_time_ms) "
                   "FROM inspections i LEFT JOIN products p ON p.product_id = i.product_id" +
                       detail::inspectionWhereClause(filter) +
                       " GROUP BY i.product_id ORDER BY 1");
    detail::bindInspectionFilter(stmt, filter);
    std::vector<GroupedProcessingTime> rows;
    while (stmt.step()) rows.push_back({stmt.columnText(0), readTimeStats(stmt, 1)});
    return rows;
}

}  // namespace optinspect::database
