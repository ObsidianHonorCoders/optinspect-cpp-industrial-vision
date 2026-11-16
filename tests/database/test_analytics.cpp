#include <gtest/gtest.h>

#include "TempDatabase.hpp"
#include "database/DatabaseConnection.hpp"
#include "database/DatabaseInitializer.hpp"
#include "database/DefectRepository.hpp"
#include "database/InspectionPersistenceService.hpp"
#include "database/InspectionRepository.hpp"
#include "database/ProductRepository.hpp"
#include "database/QualityAnalyticsService.hpp"

#include <map>
#include <optional>
#include <vector>

using namespace optinspect::database;
using optinspect::testing::TempDatabaseFile;

namespace {

/// Deterministic synthetic dataset with hand-computed expectations. The
/// values are invented test data used only to verify the SQL; they are not
/// measurements of any real inspection.
///
///  #  product   status  timestamp (UTC)        time ms  defects (area:score)
///  1  PCB-A     PASS    2026-01-05T08:00:00Z   100      -
///  2  PCB-A     PASS    2026-01-05T09:00:00Z   110      -
///  3  PCB-A     FAIL    2026-01-06T10:00:00Z   200      100:.05 200:.10 300:.15
///  4  PCB-A     PASS    2026-01-06T11:00:00Z   120      -
///  5  PCB-B     PASS    2026-01-12T08:00:00Z   130      50:.025
///  6  PCB-B     FAIL    2026-01-12T09:00:00Z   300      400:.20 500:.25 600:.30 700:.35 2000:1.0
///  7  PCB-B     PASS    2026-02-02T08:00:00Z   140      -
///  8  PCB-B     FAIL    2026-02-02T09:00:00Z   250      1200:.60 250:.125
///  9  PCB-C     PASS    2026-02-03T08:00:00Z   (none)   -
/// 10  (none)    REVIEW  2026-02-03T09:00:00Z   400      -
class AnalyticsTest : public ::testing::Test {
protected:
    AnalyticsTest()
        : file("analytics"), db(DatabaseConnection::open(file.path())), analytics(db) {
        DatabaseInitializer::initialize(db);
        load();
    }

    struct Row {
        const char* product;
        InspectionStatus status;
        const char* timestamp;
        std::optional<double> time_ms;
        std::vector<std::pair<double, double>> defects;  // area, score
        const char* serial;
    };

    std::int64_t productId(const char* code) { return product_ids.at(code); }

    void load() {
        ProductRepository products(db);
        for (const char* code : {"PCB-A", "PCB-B", "PCB-C"}) {
            Product p;
            p.code = code;
            p.name = std::string("Board ") + code;
            product_ids[code] = products.create(p);
        }

        using S = InspectionStatus;
        const std::vector<Row> rows = {
            {"PCB-A", S::PASS, "2026-01-05T08:00:00Z", 100.0, {}, nullptr},
            {"PCB-A", S::PASS, "2026-01-05T09:00:00Z", 110.0, {}, nullptr},
            {"PCB-A", S::FAIL, "2026-01-06T10:00:00Z", 200.0, {{100, .05}, {200, .10}, {300, .15}}, "SN-0003"},
            {"PCB-A", S::PASS, "2026-01-06T11:00:00Z", 120.0, {}, nullptr},
            {"PCB-B", S::PASS, "2026-01-12T08:00:00Z", 130.0, {{50, .025}}, nullptr},
            {"PCB-B", S::FAIL, "2026-01-12T09:00:00Z", 300.0,
             {{400, .20}, {500, .25}, {600, .30}, {700, .35}, {2000, 1.0}}, nullptr},
            {"PCB-B", S::PASS, "2026-02-02T08:00:00Z", 140.0, {}, nullptr},
            {"PCB-B", S::FAIL, "2026-02-02T09:00:00Z", 250.0, {{1200, .60}, {250, .125}}, nullptr},
            {"PCB-C", S::PASS, "2026-02-03T08:00:00Z", std::nullopt, {}, nullptr},
            {nullptr, S::REVIEW, "2026-02-03T09:00:00Z", 400.0, {}, nullptr},
        };

        InspectionPersistenceService service(db);
        for (const Row& row : rows) {
            InspectionPersistenceRequest request;
            if (row.product) request.inspection.product_id = productId(row.product);
            request.inspection.status = row.status;
            request.inspection.inspection_timestamp = row.timestamp;
            request.inspection.processing_time_ms = row.time_ms;
            if (row.serial) request.inspection.serial_number = row.serial;
            for (const auto& [area, score] : row.defects) {
                DefectRecord d;
                d.width = 10;
                d.height = 10;
                d.area = area;
                d.confidence = score;
                request.defects.push_back(d);
            }
            service.persist(request);
        }
    }

    TempDatabaseFile file;
    DatabaseConnection db;
    QualityAnalyticsService analytics;
    std::map<std::string, std::int64_t> product_ids;
};

}  // namespace

TEST_F(AnalyticsTest, SummaryCountsAndRates) {
    const InspectionSummary s = analytics.summary();
    EXPECT_EQ(s.total, 10);
    EXPECT_EQ(s.pass, 6);
    EXPECT_EQ(s.fail, 3);
    EXPECT_EQ(s.review, 1);
    EXPECT_DOUBLE_EQ(s.pass_rate, 0.6);
    EXPECT_DOUBLE_EQ(s.fail_rate, 0.3);
    EXPECT_DOUBLE_EQ(s.review_rate, 0.1);
}

TEST_F(AnalyticsTest, SummaryRespectsFilters) {
    InspectionFilter product_a;
    product_a.product_id = productId("PCB-A");
    const InspectionSummary a = analytics.summary(product_a);
    EXPECT_EQ(a.total, 4);
    EXPECT_EQ(a.fail, 1);
    EXPECT_DOUBLE_EQ(a.fail_rate, 0.25);

    InspectionFilter january_6;
    january_6.from = "2026-01-06";
    january_6.to = "2026-01-06";
    EXPECT_EQ(analytics.summary(january_6).total, 2);

    InspectionFilter fails;
    fails.status = InspectionStatus::FAIL;
    const InspectionSummary f = analytics.summary(fails);
    EXPECT_EQ(f.total, 3);
    EXPECT_DOUBLE_EQ(f.fail_rate, 1.0);

    InspectionFilter serial;
    serial.serial_number = "SN-0003";
    EXPECT_EQ(analytics.summary(serial).total, 1);
}

TEST_F(AnalyticsTest, EmptyDatabaseNeverDividesByZero) {
    DatabaseConnection empty = DatabaseConnection::openInMemory();
    DatabaseInitializer::initialize(empty);
    QualityAnalyticsService service(empty);

    const InspectionSummary s = service.summary();
    EXPECT_EQ(s.total, 0);
    EXPECT_DOUBLE_EQ(s.pass_rate, 0.0);
    EXPECT_DOUBLE_EQ(s.fail_rate, 0.0);

    const DefectStatistics d = service.defectStatistics();
    EXPECT_EQ(d.inspections, 0);
    EXPECT_EQ(d.total_defects, 0);
    EXPECT_DOUBLE_EQ(d.avg_defects_per_inspection, 0.0);
    EXPECT_FALSE(d.avg_defect_area_px.has_value());
    EXPECT_TRUE(d.by_confidence_band.empty());

    EXPECT_TRUE(service.productQuality().empty());
    EXPECT_TRUE(service.timeSeries(TimeGranularity::Day).empty());
    const ProcessingTimeStats t = service.processingTime();
    EXPECT_EQ(t.samples, 0);
    EXPECT_FALSE(t.avg_ms.has_value());
}

TEST_F(AnalyticsTest, DefectStatistics) {
    const DefectStatistics d = analytics.defectStatistics();
    EXPECT_EQ(d.inspections, 10);
    EXPECT_EQ(d.total_defects, 11);
    EXPECT_DOUBLE_EQ(d.avg_defects_per_inspection, 1.1);  // zero-defect inspections included
    EXPECT_EQ(d.max_defects_in_inspection, 5);
    ASSERT_TRUE(d.avg_defect_area_px.has_value());
    EXPECT_NEAR(*d.avg_defect_area_px, 6300.0 / 11.0, 1e-9);

    std::map<std::string, int> bands;
    for (const auto& b : d.by_confidence_band) bands[b.band] = b.count;
    EXPECT_EQ(bands["LOW"], 8);
    EXPECT_EQ(bands["MEDIUM"], 2);
    EXPECT_EQ(bands["HIGH"], 1);
    ASSERT_EQ(d.by_confidence_band.size(), 3u);
    EXPECT_EQ(d.by_confidence_band.front().band, "LOW");  // ordered by score
    EXPECT_EQ(d.by_confidence_band.back().band, "HIGH");
}

TEST_F(AnalyticsTest, DefectStatisticsRespectFilters) {
    InspectionFilter product_b;
    product_b.product_id = productId("PCB-B");
    const DefectStatistics d = analytics.defectStatistics(product_b);
    EXPECT_EQ(d.inspections, 4);
    EXPECT_EQ(d.total_defects, 8);
    EXPECT_DOUBLE_EQ(d.avg_defects_per_inspection, 2.0);
    EXPECT_EQ(d.max_defects_in_inspection, 5);
}

TEST_F(AnalyticsTest, ProductQuality) {
    const auto rows = analytics.productQuality();
    ASSERT_EQ(rows.size(), 4u);

    EXPECT_EQ(rows[0].product_code, "PCB-B");  // highest failure rate first
    EXPECT_EQ(rows[0].total, 4);
    EXPECT_EQ(rows[0].failed, 2);
    EXPECT_DOUBLE_EQ(rows[0].failure_rate, 0.5);
    EXPECT_DOUBLE_EQ(*rows[0].avg_processing_time_ms, 205.0);

    EXPECT_EQ(rows[1].product_code, "PCB-A");
    EXPECT_EQ(rows[1].total, 4);
    EXPECT_EQ(rows[1].failed, 1);
    EXPECT_DOUBLE_EQ(rows[1].failure_rate, 0.25);
    EXPECT_DOUBLE_EQ(*rows[1].avg_processing_time_ms, 132.5);

    EXPECT_EQ(rows[2].product_code, "(unassigned)");
    EXPECT_FALSE(rows[2].product_id.has_value());
    EXPECT_EQ(rows[2].review, 1);
    EXPECT_EQ(rows[2].failed, 0);

    EXPECT_EQ(rows[3].product_code, "PCB-C");
    EXPECT_EQ(rows[3].total, 1);
    EXPECT_FALSE(rows[3].avg_processing_time_ms.has_value());  // no recorded time
}

TEST_F(AnalyticsTest, ProductQualityHavingClauseFiltersSmallSamples) {
    const auto rows = analytics.productQuality({}, 2);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].product_code, "PCB-B");
    EXPECT_EQ(rows[1].product_code, "PCB-A");
}

TEST_F(AnalyticsTest, DailyTimeSeriesWithWindowFunctions) {
    const auto rows = analytics.timeSeries(TimeGranularity::Day);
    ASSERT_EQ(rows.size(), 5u);

    const std::vector<std::string> periods = {"2026-01-05", "2026-01-06", "2026-01-12", "2026-02-02",
                                              "2026-02-03"};
    const std::vector<int> fails = {0, 1, 1, 1, 0};
    const std::vector<int> defects = {0, 3, 6, 2, 0};
    const std::vector<int> cumulative = {2, 4, 6, 8, 10};
    for (std::size_t i = 0; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].period, periods[i]);
        EXPECT_EQ(rows[i].total, 2);
        EXPECT_EQ(rows[i].fail, fails[i]);
        EXPECT_EQ(rows[i].total_defects, defects[i]);
        EXPECT_EQ(rows[i].cumulative_inspections, cumulative[i]);
        EXPECT_DOUBLE_EQ(rows[i].fail_rate, fails[i] / 2.0);
    }
    EXPECT_EQ(rows[4].review, 1);

    EXPECT_FALSE(rows[0].defects_per_inspection_change.has_value());
    EXPECT_DOUBLE_EQ(*rows[1].defects_per_inspection_change, 1.5);
    EXPECT_DOUBLE_EQ(*rows[2].defects_per_inspection_change, 1.5);
    EXPECT_DOUBLE_EQ(*rows[3].defects_per_inspection_change, -2.0);
    EXPECT_DOUBLE_EQ(*rows[4].defects_per_inspection_change, -1.0);
}

TEST_F(AnalyticsTest, MonthlyAndWeeklyTimeSeries) {
    const auto months = analytics.timeSeries(TimeGranularity::Month);
    ASSERT_EQ(months.size(), 2u);
    EXPECT_EQ(months[0].period, "2026-01");
    EXPECT_EQ(months[0].total, 6);
    EXPECT_EQ(months[0].pass, 4);
    EXPECT_EQ(months[0].fail, 2);
    EXPECT_EQ(months[0].total_defects, 9);
    EXPECT_EQ(months[1].period, "2026-02");
    EXPECT_EQ(months[1].total, 4);
    EXPECT_EQ(months[1].pass, 2);
    EXPECT_EQ(months[1].fail, 1);
    EXPECT_EQ(months[1].review, 1);
    EXPECT_EQ(months[1].cumulative_inspections, 10);
    EXPECT_DOUBLE_EQ(*months[1].defects_per_inspection_change, 0.5 - 1.5);

    const auto weeks = analytics.timeSeries(TimeGranularity::Week);
    ASSERT_EQ(weeks.size(), 3u);
    EXPECT_EQ(weeks[0].period, "2026-W01");
    EXPECT_EQ(weeks[0].total, 4);
    EXPECT_EQ(weeks[1].period, "2026-W02");
    EXPECT_EQ(weeks[1].total, 2);
    EXPECT_EQ(weeks[2].period, "2026-W05");
    EXPECT_EQ(weeks[2].total, 4);
}

TEST_F(AnalyticsTest, ProcessingTimeStatistics) {
    const ProcessingTimeStats all = analytics.processingTime();
    EXPECT_EQ(all.samples, 9);  // inspection 9 has no recorded time and is excluded
    EXPECT_NEAR(*all.avg_ms, 1750.0 / 9.0, 1e-9);
    EXPECT_DOUBLE_EQ(*all.min_ms, 100.0);
    EXPECT_DOUBLE_EQ(*all.max_ms, 400.0);

    const auto by_status = analytics.processingTimeByStatus();
    ASSERT_EQ(by_status.size(), 3u);
    EXPECT_EQ(by_status[0].group, "FAIL");
    EXPECT_EQ(by_status[0].stats.samples, 3);
    EXPECT_DOUBLE_EQ(*by_status[0].stats.avg_ms, 250.0);
    EXPECT_DOUBLE_EQ(*by_status[0].stats.min_ms, 200.0);
    EXPECT_DOUBLE_EQ(*by_status[0].stats.max_ms, 300.0);
    EXPECT_EQ(by_status[1].group, "PASS");
    EXPECT_EQ(by_status[1].stats.samples, 5);
    EXPECT_DOUBLE_EQ(*by_status[1].stats.avg_ms, 120.0);
    EXPECT_EQ(by_status[2].group, "REVIEW");
    EXPECT_DOUBLE_EQ(*by_status[2].stats.avg_ms, 400.0);

    const auto by_product = analytics.processingTimeByProduct();
    ASSERT_EQ(by_product.size(), 4u);
    EXPECT_EQ(by_product[0].group, "(unassigned)");
    EXPECT_EQ(by_product[1].group, "PCB-A");
    EXPECT_DOUBLE_EQ(*by_product[1].stats.avg_ms, 132.5);
    EXPECT_EQ(by_product[2].group, "PCB-B");
    EXPECT_DOUBLE_EQ(*by_product[2].stats.avg_ms, 205.0);
    EXPECT_EQ(by_product[3].group, "PCB-C");
    EXPECT_EQ(by_product[3].stats.samples, 0);
    EXPECT_FALSE(by_product[3].stats.avg_ms.has_value());
}

TEST_F(AnalyticsTest, HistoryFiltersMatchSummary) {
    InspectionRepository repo(db);
    InspectionFilter filter;
    filter.product_id = productId("PCB-B");
    filter.status = InspectionStatus::FAIL;
    filter.from = "2026-01-01";
    filter.to = "2026-01-31";
    EXPECT_EQ(repo.list(filter).size(), 1u);
    EXPECT_EQ(analytics.summary(filter).total, 1);
}
