#include "PerformanceReport.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace optinspect {

namespace {

const char* alignmentName(AlignmentMethod m) {
    switch (m) {
        case AlignmentMethod::ECC: return "ECC";
        case AlignmentMethod::ORB: return "ORB";
        case AlignmentMethod::NONE: return "NONE";
    }
    return "UNKNOWN";
}

const char* thresholdName(ThresholdMethod m) {
    switch (m) {
        case ThresholdMethod::FIXED: return "FIXED";
        case ThresholdMethod::OTSU: return "OTSU";
        case ThresholdMethod::ADAPTIVE_MEAN: return "ADAPTIVE_MEAN";
        case ThresholdMethod::ADAPTIVE_GAUSSIAN: return "ADAPTIVE_GAUSSIAN";
    }
    return "UNKNOWN";
}

std::string fmt(double v, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << v;
    return oss.str();
}

void statsRow(std::ostream& out, const std::string& label, const TimingStats& s) {
    out << "| " << label << " | " << fmt(s.min, 1) << " | " << fmt(s.median, 1) << " | "
        << fmt(s.mean, 1) << " | " << fmt(s.max, 1) << " |\n";
}

}  // namespace

void classifyAgainstDecisionRules(PerformanceRecord& record,
                                  const std::vector<double>& defect_areas_px,
                                  bool alignment_ok, const DecisionConfig& rules) {
    record.regions_meeting_area_rule = static_cast<std::size_t>(
        std::count_if(defect_areas_px.begin(), defect_areas_px.end(),
                      [&](double a) { return a >= rules.fail_area_px; }));

    if (!alignment_ok || record.alignment_confidence < rules.min_alignment_confidence) {
        record.decided_by = "alignment";
    } else if (record.regions_meeting_area_rule > 0) {
        record.decided_by = "area";
    } else if (static_cast<int>(defect_areas_px.size()) >= rules.fail_defect_count_threshold) {
        record.decided_by = "count";
    } else {
        record.decided_by = "none";
    }
}

TimingStats computeStats(std::vector<double> values) {
    TimingStats s;
    if (values.empty()) {
        return s;
    }
    std::sort(values.begin(), values.end());
    s.min = values.front();
    s.max = values.back();
    s.mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    const size_t n = values.size();
    s.median = (n % 2 == 1) ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
    return s;
}

void writePerformanceReport(const std::string& output_path, const std::string& run_label,
                            const std::vector<PerformanceRecord>& records,
                            const InspectionConfig& config) {
    // Stage names in order of first appearance across all records.
    std::vector<std::string> stage_names;
    for (const auto& r : records) {
        for (const auto& st : r.stages) {
            if (std::find(stage_names.begin(), stage_names.end(), st.stage_name) ==
                stage_names.end()) {
                stage_names.push_back(st.stage_name);
            }
        }
    }

    std::ofstream out(output_path);
    out << "# OptiInspect Performance Report\n\n";
    out << "Run: `" << run_label << "`  \n";
    out << "Pairs inspected: " << records.size() << "\n\n";
    out << "All figures are measured by OptiInspect during this run (single execution per pair, "
           "no warm-up or repetition). \"Total\" is wall-clock time per pair including image "
           "loading and writing output files; stage columns come from the pipeline's own "
           "stage timers.\n\n";

    out << "## Configuration used\n\n";
    out << "- Alignment: " << alignmentName(config.alignment.method) << "\n";
    out << "- Threshold: " << thresholdName(config.threshold.method) << "\n";
    out << "- Morphology kernel: " << config.morphology.kernel_size << "\n";
    out << "- Defect area range: " << config.detection.min_defect_area_px << " - "
        << config.detection.max_defect_area_px << " px\n";
    out << "- Decision: fail_defect_count_threshold=" << config.decision.fail_defect_count_threshold
        << ", fail_area_px=" << config.decision.fail_area_px
        << ", min_alignment_confidence=" << config.decision.min_alignment_confidence << "\n\n";

    out << "## Decision rules applied\n\n";
    out << "Evaluated in this order; the first that applies decides:\n\n";
    out << "1. **REVIEW** if alignment failed or alignment confidence < "
        << config.decision.min_alignment_confidence << ".\n";
    out << "2. **FAIL (area rule)** if any region has area >= " << config.decision.fail_area_px
        << " px.\n";
    out << "3. **FAIL (count rule)** if the number of regions >= "
        << config.decision.fail_defect_count_threshold << ".\n";
    out << "4. **PASS** otherwise.\n\n";
    out << "\"Meet area rule\" counts regions with area >= " << config.decision.fail_area_px
        << " px; \"Below area rule\" are the remaining detected regions. Regions below the area "
           "limit still count toward the count rule.\n\n";

    out << "## Results\n\n";
    out << "| Pair | Decision | Decided by | Regions | Meet area rule | Below area rule | Alignment confidence |";
    for (const auto& name : stage_names) out << " " << name << " (ms) |";
    out << " Total (ms) |\n";
    out << "|---|---|---|---|---|---|---|";
    for (size_t i = 0; i < stage_names.size(); ++i) out << "---|";
    out << "---|\n";

    for (const auto& r : records) {
        if (r.decision == "ERROR") {
            out << "| " << r.id << " | ERROR | - | - | - | - | - |";
            for (size_t i = 0; i < stage_names.size(); ++i) out << " - |";
            out << " - |\n";
            continue;
        }
        out << "| " << r.id << " | " << r.decision << " | " << r.decided_by << " | " << r.defect_count
            << " | " << r.regions_meeting_area_rule << " | "
            << (r.defect_count - r.regions_meeting_area_rule) << " | "
            << fmt(r.alignment_confidence, 3) << " |";
        for (const auto& name : stage_names) {
            const auto it = std::find_if(r.stages.begin(), r.stages.end(),
                                         [&](const StageTiming& s) { return s.stage_name == name; });
            out << " " << (it == r.stages.end() ? std::string("-") : fmt(it->milliseconds, 2))
                << " |";
        }
        out << " " << fmt(r.total_ms, 1) << " |\n";
    }

    // Statistics only over pairs that completed.
    std::vector<const PerformanceRecord*> ok;
    for (const auto& r : records) {
        if (r.decision != "ERROR") ok.push_back(&r);
    }

    {
        std::size_t total_regions = 0, meet = 0;
        std::size_t by_area = 0, by_count = 0, by_alignment = 0, by_none = 0;
        for (const auto* r : ok) {
            total_regions += r->defect_count;
            meet += r->regions_meeting_area_rule;
            if (r->decided_by == "area") ++by_area;
            else if (r->decided_by == "count") ++by_count;
            else if (r->decided_by == "alignment") ++by_alignment;
            else ++by_none;
        }
        out << "\n## Regions vs. decision rules (" << ok.size() << " successful pair(s))\n\n";
        out << "| | Regions |\n|---|---|\n";
        out << "| Total regions detected | " << total_regions << " |\n";
        out << "| Meet the area rule (area >= " << config.decision.fail_area_px << " px) | " << meet
            << " |\n";
        out << "| Do not meet the area rule | " << (total_regions - meet) << " |\n\n";
        out << "Pairs decided by: area rule " << by_area << ", count rule " << by_count
            << ", alignment " << by_alignment << ", no rule (PASS) " << by_none << ".\n";
    }

    out << "\n## Summary (ms, " << ok.size() << " successful pair(s))\n\n";
    if (ok.empty()) {
        out << "No pair completed successfully.\n";
    } else {
        out << "| Metric | Min | Median | Mean | Max |\n|---|---|---|---|---|\n";
        for (const auto& name : stage_names) {
            std::vector<double> values;
            for (const auto* r : ok) {
                for (const auto& st : r->stages) {
                    if (st.stage_name == name) values.push_back(st.milliseconds);
                }
            }
            if (!values.empty()) statsRow(out, name, computeStats(values));
        }
        std::vector<double> totals;
        for (const auto* r : ok) totals.push_back(r->total_ms);
        statsRow(out, "**total**", computeStats(totals));

        const double sum_total = std::accumulate(totals.begin(), totals.end(), 0.0);
        out << "\nSum of per-pair totals: " << fmt(sum_total, 1) << " ms\n";
    }

    const auto errors = std::count_if(records.begin(), records.end(),
                                      [](const PerformanceRecord& r) { return r.decision == "ERROR"; });
    if (errors > 0) {
        out << "\n## Errors\n\n";
        for (const auto& r : records) {
            if (r.decision == "ERROR") out << "- " << r.id << ": " << r.error << "\n";
        }
    }
}

}  // namespace optinspect
