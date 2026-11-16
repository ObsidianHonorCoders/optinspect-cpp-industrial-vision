#pragma once

#include "common/Config.hpp"
#include "common/Timing.hpp"

#include <string>
#include <vector>

namespace optinspect {

/// Measurements of one inspection (one reference/inspected pair) in a run.
struct PerformanceRecord {
    std::string id;
    std::string decision;             ///< "PASS"/"FAIL"/"REVIEW", or "ERROR".
    std::size_t defect_count = 0;
    /// Regions whose area_px >= decision.fail_area_px (the per-region FAIL rule).
    std::size_t regions_meeting_area_rule = 0;
    /// Which rule produced the decision: "area", "count", "alignment" or "none".
    std::string decided_by = "none";
    double alignment_confidence = 0.0;
    std::vector<StageTiming> stages;  ///< Per-stage timings from the pipeline.
    double total_ms = 0.0;            ///< Wall-clock incl. image I/O and writing outputs.
    std::string error;                ///< Non-empty only when decision == "ERROR".
};

struct TimingStats {
    double min = 0.0;
    double median = 0.0;
    double mean = 0.0;
    double max = 0.0;
};

/// Fills regions_meeting_area_rule and decided_by from a finished inspection,
/// mirroring the rule order in InspectionDecider (alignment, then area, then
/// count). `defect_areas_px` are the areas of all accepted regions.
void classifyAgainstDecisionRules(PerformanceRecord& record,
                                  const std::vector<double>& defect_areas_px,
                                  bool alignment_ok, const DecisionConfig& rules);

/// Computes min/median/mean/max of `values`; all zeros if empty.
TimingStats computeStats(std::vector<double> values);

/// Writes a Markdown performance report for one CLI run, built only from the
/// measurements in `records` and the configuration actually used.
/// `run_label` identifies the run (e.g. its timestamp).
void writePerformanceReport(const std::string& output_path, const std::string& run_label,
                            const std::vector<PerformanceRecord>& records,
                            const InspectionConfig& config);

}  // namespace optinspect
