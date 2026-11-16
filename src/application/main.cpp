#include "BatchDiscovery.hpp"
#include "CliArguments.hpp"
#include "InspectionPipeline.hpp"
#include "PerformanceReport.hpp"
#include "ReportWriter.hpp"
#include "ResultAnnotator.hpp"

#include "acquisition/FileImageSource.hpp"
#include "common/Errors.hpp"
#include "common/Timing.hpp"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

namespace fs = std::filesystem;
using namespace optinspect;

/// Generates a reasonably unique inspection identifier from the current
/// wall-clock time, in the form YYYYMMDD-HHMMSS.
std::string generateInspectionId() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &now_c);
#else
    localtime_r(&now_c, &local_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y%m%d-%H%M%S");
    return oss.str();
}

int exitCodeForDecision(InspectionDecision decision) {
    switch (decision) {
        case InspectionDecision::PASS: return 0;
        case InspectionDecision::FAIL: return 1;
        case InspectionDecision::REVIEW: return 2;
    }
    return 3;
}

struct InspectionOutcome {
    InspectionResult result;
    std::string annotated_path;
    std::string diff_path;
    std::string report_path;
    std::vector<StageTiming> stages;
    double total_ms = 0.0;
};

/// Runs one reference/inspected inspection and writes its annotated image,
/// difference image and JSON report into `output_dir`, all named after
/// `inspection_id`.
InspectionOutcome runInspection(const InspectionConfig& config,
                                const std::string& reference_path,
                                const std::string& inspected_path,
                                const std::string& output_dir,
                                const std::string& inspection_id) {
    Stopwatch total_timer;

    const cv::Mat reference = FileImageSource(reference_path).load();
    const cv::Mat inspected = FileImageSource(inspected_path).load();

    const InspectionPipeline pipeline(config);
    const PipelineOutput output = pipeline.run(reference, inspected);

    fs::create_directories(output_dir);

    InspectionOutcome outcome;
    outcome.result = output.result;
    outcome.stages = output.timer.timings();

    const cv::Mat& annotation_source =
        output.aligned_inspected.empty() ? inspected : output.aligned_inspected;
    const cv::Mat annotated = ResultAnnotator::annotate(annotation_source, output.result);
    outcome.annotated_path = (fs::path(output_dir) / (inspection_id + "_annotated.png")).string();
    cv::imwrite(outcome.annotated_path, annotated);

    if (!output.difference_image.empty()) {
        outcome.diff_path = (fs::path(output_dir) / (inspection_id + "_difference.png")).string();
        cv::imwrite(outcome.diff_path, output.difference_image);
    }

    InspectionMetadata metadata;
    metadata.inspection_id = inspection_id;
    metadata.reference_path = reference_path;
    metadata.inspected_path = inspected_path;
    metadata.reference_size = reference.size();
    metadata.inspected_size = inspected.size();

    outcome.report_path = (fs::path(output_dir) / (inspection_id + "_report.json")).string();
    ReportWriter::writeJson(outcome.report_path, metadata, output.result, output.timer, config);

    outcome.total_ms = total_timer.elapsedMilliseconds();
    return outcome;
}

PerformanceRecord toRecord(const std::string& id, const InspectionOutcome& o,
                         const DecisionConfig& rules) {
    PerformanceRecord r;
    r.id = id;
    r.decision = toString(o.result.decision);
    r.defect_count = o.result.defects.size();
    r.alignment_confidence = o.result.alignment_confidence;
    r.stages = o.stages;
    r.total_ms = o.total_ms;
    std::vector<double> areas;
    for (const auto& d : o.result.defects) areas.push_back(d.area_px);
    classifyAgainstDecisionRules(r, areas, o.result.alignment_succeeded, rules);
    return r;
}

void printSingleSummary(const std::string& inspection_id, const std::string& reference_path,
                        const std::string& inspected_path, const InspectionOutcome& o) {
    std::cout << "=== OptiInspect Inspection Summary ===\n";
    std::cout << "Inspection ID : " << inspection_id << "\n";
    std::cout << "Reference     : " << reference_path << "\n";
    std::cout << "Inspected     : " << inspected_path << "\n";
    std::cout << "Alignment     : " << (o.result.alignment_succeeded ? "OK" : "FAILED")
              << " (confidence " << o.result.alignment_confidence << ")\n";
    std::cout << "Decision      : " << toString(o.result.decision) << "\n";
    std::cout << "Reason        : " << o.result.decision_reason << "\n";
    std::cout << "Defects found : " << o.result.defects.size() << "\n";
    std::cout << "Annotated img : " << o.annotated_path << "\n";
    if (!o.diff_path.empty()) {
        std::cout << "Difference img: " << o.diff_path << "\n";
    }
    std::cout << "Report        : " << o.report_path << "\n";
    std::cout << "Total time    : " << o.total_ms << " ms\n";
}

/// Batch mode: both --reference and --input are directories. Every
/// "<id>_temp.*" in the reference directory is inspected against the
/// "<id>_test.*" in the input directory.
/// Exit code: 70 if any pair errored or none were found, else 1 if any pair
/// FAILed, else 2 if any needs REVIEW, else 0.
int runBatch(const CliArguments& args, const InspectionConfig& config) {
    const BatchDiscovery discovery = discoverPairs(args.reference_path, args.inspected_path);

    for (const auto& p : discovery.unmatched_references) {
        std::cerr << "Warning: no matching _test image for " << p.string() << "\n";
    }
    for (const auto& p : discovery.unmatched_inspected) {
        std::cerr << "Warning: no matching _temp image for " << p.string() << "\n";
    }
    if (discovery.pairs.empty()) {
        std::cerr << "Error: no <id>_temp / <id>_test image pairs found.\n";
        return 70;
    }

    const std::string run_stamp = generateInspectionId();
    std::cout << "=== OptiInspect Batch Inspection: " << discovery.pairs.size() << " pair(s) ===\n";
    std::cout << std::left << std::setw(12) << "Pair" << std::setw(9) << "Decision"
              << std::setw(9) << "Defects" << std::setw(11) << "Align conf" << std::setw(11)
              << "Time (ms)" << "Report\n";

    int pass = 0, fail = 0, review = 0, errors = 0;
    std::vector<PerformanceRecord> records;
    for (const auto& pair : discovery.pairs) {
        const std::string inspection_id = run_stamp + "-" + pair.id;
        try {
            const InspectionOutcome o =
                runInspection(config, pair.reference_path.string(), pair.inspected_path.string(),
                              args.output_dir, inspection_id);
            records.push_back(toRecord(pair.id, o, config.decision));
            switch (o.result.decision) {
                case InspectionDecision::PASS: ++pass; break;
                case InspectionDecision::FAIL: ++fail; break;
                case InspectionDecision::REVIEW: ++review; break;
            }
            std::cout << std::left << std::setw(12) << pair.id << std::setw(9)
                      << toString(o.result.decision) << std::setw(9) << o.result.defects.size()
                      << std::setw(11) << std::fixed << std::setprecision(3)
                      << o.result.alignment_confidence << std::setw(11) << std::setprecision(1)
                      << o.total_ms << o.report_path << "\n";
        } catch (const std::exception& e) {
            ++errors;
            PerformanceRecord failed;
            failed.id = pair.id;
            failed.decision = "ERROR";
            failed.error = e.what();
            records.push_back(failed);
            std::cout << std::left << std::setw(12) << pair.id << "ERROR    " << e.what() << "\n";
        }
    }

    std::cout << "\nTotal: " << discovery.pairs.size() << "  PASS: " << pass << "  FAIL: " << fail
              << "  REVIEW: " << review << "  ERROR: " << errors << "\n";
    const std::string perf_path =
        (fs::path(args.output_dir) / (run_stamp + "_performance_report.md")).string();
    writePerformanceReport(perf_path, run_stamp, records, config);
    std::cout << "Performance   : " << perf_path << "\n";
    std::cout << "Outputs written to: " << args.output_dir << "\n";

    if (errors > 0) return 70;
    if (fail > 0) return 1;
    if (review > 0) return 2;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    CliArguments args;
    try {
        args = parseArguments(argc, argv);
    } catch (const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << "\n\n" << usageText();
        return 64;  // EX_USAGE
    }

    if (args.show_help) {
        std::cout << usageText();
        return 0;
    }

    try {
        const InspectionConfig config = InspectionConfig::loadFromFile(args.config_path);

        const bool ref_is_dir = fs::is_directory(args.reference_path);
        const bool insp_is_dir = fs::is_directory(args.inspected_path);
        if (ref_is_dir != insp_is_dir) {
            std::cerr << "Error: --reference and --input must both be files or both be directories.\n";
            return 64;
        }
        if (ref_is_dir) {
            return runBatch(args, config);
        }

        const std::string inspection_id = generateInspectionId();
        const InspectionOutcome outcome = runInspection(
            config, args.reference_path, args.inspected_path, args.output_dir, inspection_id);
        const std::string perf_path =
            (fs::path(args.output_dir) / (inspection_id + "_performance_report.md")).string();
        writePerformanceReport(perf_path, inspection_id, {toRecord(inspection_id, outcome, config.decision)}, config);
        printSingleSummary(inspection_id, args.reference_path, args.inspected_path, outcome);
        std::cout << "Performance   : " << perf_path << "\n";
        return exitCodeForDecision(outcome.result.decision);

    } catch (const OptInspectError& e) {
        std::cerr << "Inspection failed: " << e.what() << "\n";
        return 70;  // EX_SOFTWARE
    } catch (const cv::Exception& e) {
        std::cerr << "OpenCV error: " << e.what() << "\n";
        return 70;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << "\n";
        return 70;
    }
}