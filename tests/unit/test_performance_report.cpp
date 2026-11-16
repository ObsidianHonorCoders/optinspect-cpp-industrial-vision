#include "PerformanceReport.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace optinspect;

namespace {

std::string readAll(const fs::path& p) {
    std::ifstream in(p.string());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

PerformanceRecord makeRecord(const std::string& id, double align_ms, double total_ms) {
    PerformanceRecord r;
    r.id = id;
    r.decision = "FAIL";
    r.defect_count = 3;
    r.regions_meeting_area_rule = 1;
    r.decided_by = "area";
    r.alignment_confidence = 0.9;
    r.stages = {{"alignment", align_ms}, {"detection", 5.0}};
    r.total_ms = total_ms;
    return r;
}

}  // namespace

TEST(PerformanceReportTest, StatsOddCount) {
    const TimingStats s = computeStats({30.0, 10.0, 20.0});
    EXPECT_DOUBLE_EQ(s.min, 10.0);
    EXPECT_DOUBLE_EQ(s.median, 20.0);
    EXPECT_DOUBLE_EQ(s.mean, 20.0);
    EXPECT_DOUBLE_EQ(s.max, 30.0);
}

TEST(PerformanceReportTest, StatsEvenCountAndEmpty) {
    EXPECT_DOUBLE_EQ(computeStats({1.0, 2.0, 3.0, 10.0}).median, 2.5);
    const TimingStats empty = computeStats({});
    EXPECT_DOUBLE_EQ(empty.max, 0.0);
    EXPECT_DOUBLE_EQ(empty.median, 0.0);
}

TEST(PerformanceReportTest, ClassifiesRegionsAgainstDecisionRules) {
    DecisionConfig rules;
    rules.fail_area_px = 400.0;
    rules.fail_defect_count_threshold = 2;
    rules.min_alignment_confidence = 0.5;

    PerformanceRecord area;
    area.alignment_confidence = 0.9;
    classifyAgainstDecisionRules(area, {399.0, 400.0, 900.0}, true, rules);
    EXPECT_EQ(area.regions_meeting_area_rule, 2u);  // >= is inclusive
    EXPECT_EQ(area.decided_by, "area");

    PerformanceRecord count;
    count.alignment_confidence = 0.9;
    classifyAgainstDecisionRules(count, {10.0, 20.0}, true, rules);
    EXPECT_EQ(count.regions_meeting_area_rule, 0u);
    EXPECT_EQ(count.decided_by, "count");

    PerformanceRecord none;
    none.alignment_confidence = 0.9;
    classifyAgainstDecisionRules(none, {10.0}, true, rules);
    EXPECT_EQ(none.decided_by, "none");

    PerformanceRecord low_conf;
    low_conf.alignment_confidence = 0.2;
    classifyAgainstDecisionRules(low_conf, {900.0}, true, rules);
    EXPECT_EQ(low_conf.decided_by, "alignment");  // alignment outranks area

    PerformanceRecord failed;
    failed.alignment_confidence = 0.9;
    classifyAgainstDecisionRules(failed, {}, false, rules);
    EXPECT_EQ(failed.decided_by, "alignment");
}

TEST(PerformanceReportTest, ReportContainsRowsStatsAndErrors) {
    const fs::path path = fs::temp_directory_path() / "optinspect_perf_report_test.md";
    std::vector<PerformanceRecord> records = {makeRecord("p1", 10.0, 20.0),
                                              makeRecord("p2", 30.0, 40.0)};
    PerformanceRecord bad;
    bad.id = "p3";
    bad.decision = "ERROR";
    bad.error = "could not decode";
    records.push_back(bad);

    writePerformanceReport(path.string(), "run-x", records, InspectionConfig{});
    const std::string text = readAll(path);
    fs::remove(path);

    EXPECT_NE(text.find("Pairs inspected: 3"), std::string::npos);
    EXPECT_NE(text.find("| p1 | FAIL | area | 3 | 1 | 2 | 0.900 |"), std::string::npos);
    // 2 successful pairs x 3 regions, 1 meeting the area rule each.
    EXPECT_NE(text.find("| Total regions detected | 6 |"), std::string::npos);
    EXPECT_NE(text.find("| Do not meet the area rule | 4 |"), std::string::npos);
    EXPECT_NE(text.find("area rule 2, count rule 0"), std::string::npos);
    EXPECT_NE(text.find("alignment (ms)"), std::string::npos);
    // Stats exclude the errored pair: alignment min/median/mean/max = 10/20/20/30.
    EXPECT_NE(text.find("| alignment | 10.0 | 20.0 | 20.0 | 30.0 |"), std::string::npos);
    EXPECT_NE(text.find("| **total** | 20.0 | 30.0 | 30.0 | 40.0 |"), std::string::npos);
    EXPECT_NE(text.find("- p3: could not decode"), std::string::npos);
}
