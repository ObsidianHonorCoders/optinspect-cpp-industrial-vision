#include "BatchDiscovery.hpp"
#include "CliArguments.hpp"
#include "DatabaseCommands.hpp"
#include "InspectionPipeline.hpp"
#include "InspectionRecorder.hpp"
#include "PerformanceReport.hpp"
#include "ReportWriter.hpp"
#include "ResultAnnotator.hpp"

#include "acquisition/FileImageSource.hpp"
#include "common/Errors.hpp"
#include "common/Timing.hpp"
#include "database/TimeUtil.hpp"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
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
    cv::Size reference_size;
    cv::Size inspected_size;
    cv::Size annotated_size;
    cv::Size diff_size;
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
    outcome.reference_size = reference.size();
    outcome.inspected_size = inspected.size();

    const cv::Mat& annotation_source =
        output.aligned_inspected.empty() ? inspected : output.aligned_inspected;
    const cv::Mat annotated = ResultAnnotator::annotate(annotation_source, output.result);
    outcome.annotated_path = (fs::path(output_dir) / (inspection_id + "_annotated.png")).string();
    cv::imwrite(outcome.annotated_path, annotated);
    outcome.annotated_size = annotated.size();

    if (!output.difference_image.empty()) {
        outcome.diff_path = (fs::path(output_dir) / (inspection_id + "_difference.png")).string();
        cv::imwrite(outcome.diff_path, output.difference_image);
        outcome.diff_size = output.difference_image.size();
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

/// Absolute, normalized form of a path so stored references remain valid
/// regardless of the working directory the tool is later started from.
std::string absolutePath(const std::string& path) {
    return fs::absolute(fs::path(path)).lexically_normal().string();
}

/// How (and whether) this run stores its results in the database. Persistence
/// is strictly optional and happens after the pipeline has finished, so the
/// inspection itself never depends on the database.
struct PersistenceSession {
    bool enabled = false;
    std::string database_path;
    PersistenceOptions options;
    std::unique_ptr<InspectionRecorder> recorder;
    std::string open_error;  ///< Set when the database could not be opened.
    int failures = 0;
};

PersistenceSession makePersistenceSession(const CliArguments& args, const InspectionConfig& config) {
    PersistenceSession session;
    session.enabled = !args.no_database && (args.database_path.has_value() || config.database.enabled);
    if (!session.enabled) {
        if (args.product_id || args.serial_number || args.notes) {
            std::cerr << "Warning: --product-id/--serial/--notes are ignored because database "
                         "persistence is disabled (use --database <path>).\n";
        }
        return session;
    }

    session.database_path = args.database_path.value_or(config.database.path);
    session.options.product_id = args.product_id;
    session.options.serial_number = args.serial_number;
    session.options.notes = args.notes;

    try {
        session.recorder =
            std::make_unique<InspectionRecorder>(session.database_path, config.database.auto_initialize);
        if (session.options.product_id && !session.recorder->productExists(*session.options.product_id)) {
            session.open_error = "Product " + std::to_string(*session.options.product_id) +
                                 " does not exist in " + session.database_path;
            session.recorder.reset();
        }
    } catch (const std::exception& e) {
        session.open_error = e.what();
        session.recorder.reset();
    }
    return session;
}

/// Stores one finished inspection. Returns the new database id, or nullopt if
/// persistence is disabled or failed. A failure is reported on stderr together
/// with the command that retries it; the inspection result and files are untouched.
std::optional<std::int64_t> persistOutcome(PersistenceSession& session, const InspectionConfig& config,
                                           const std::string& inspection_id,
                                           const std::string& reference_path,
                                           const std::string& inspected_path,
                                           const InspectionOutcome& outcome) {
    if (!session.enabled) return std::nullopt;
    try {
        if (!session.recorder) throw OptInspectError(session.open_error);

        InspectionArtifacts artifacts;
        artifacts.external_id = inspection_id;
        artifacts.reference_path = absolutePath(reference_path);
        artifacts.inspected_path = absolutePath(inspected_path);
        artifacts.annotated_path = absolutePath(outcome.annotated_path);
        if (!outcome.diff_path.empty()) artifacts.difference_path = absolutePath(outcome.diff_path);
        artifacts.report_path = absolutePath(outcome.report_path);
        artifacts.reference_size = outcome.reference_size;
        artifacts.inspected_size = outcome.inspected_size;
        artifacts.annotated_size = outcome.annotated_size;
        artifacts.difference_size = outcome.diff_size;
        artifacts.processing_time_ms = outcome.total_ms;

        return session.recorder->record(buildPersistenceRequest(
            outcome.result, config, artifacts, session.options, database::nowUtcTimestamp()));
    } catch (const std::exception& e) {
        ++session.failures;
        std::cerr << "Database error: could not store inspection '" << inspection_id
                  << "': " << e.what() << "\n"
                  << "  The inspection result is unaffected and its output files were kept.\n"
                  << "  Retry with: optinspect inspection import --report \""
                  << absolutePath(outcome.report_path) << "\" --database \"" << session.database_path
                  << "\"";
        if (session.options.product_id) std::cerr << " --product-id " << *session.options.product_id;
        if (session.options.serial_number) std::cerr << " --serial \"" << *session.options.serial_number << "\"";
        std::cerr << "\n";
        return std::nullopt;
    }
}

/// Batch mode: both --reference and --input are directories. Every
/// "<id>_temp.*" in the reference directory is inspected against the
/// "<id>_test.*" in the input directory.
/// Exit code: 70 if any pair errored or none were found, else 74 if any
/// database write failed, else 1 if any pair FAILed, else 2 if any needs
/// REVIEW, else 0.
int runBatch(const CliArguments& args, const InspectionConfig& config, PersistenceSession& session) {
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
              << "Time (ms)";
    if (session.enabled) std::cout << std::setw(8) << "DB id";
    std::cout << "Report\n";

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
            const std::optional<std::int64_t> db_id =
                persistOutcome(session, config, inspection_id, pair.reference_path.string(),
                               pair.inspected_path.string(), o);
            std::cout << std::left << std::setw(12) << pair.id << std::setw(9)
                      << toString(o.result.decision) << std::setw(9) << o.result.defects.size()
                      << std::setw(11) << std::fixed << std::setprecision(3)
                      << o.result.alignment_confidence << std::setw(11) << std::setprecision(1)
                      << o.total_ms;
            if (session.enabled) {
                std::cout << std::setw(8) << (db_id ? std::to_string(*db_id) : std::string("FAILED"));
            }
            std::cout << o.report_path << "\n";
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
    if (session.enabled) {
        std::cout << "Database      : " << session.database_path << " ("
                  << (discovery.pairs.size() - static_cast<size_t>(errors) -
                      static_cast<size_t>(session.failures))
                  << " stored, " << session.failures << " failed)\n";
    }

    if (errors > 0) return 70;
    if (session.failures > 0) return 74;
    if (fail > 0) return 1;
    if (review > 0) return 2;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && isDatabaseCommand(argv[1])) {
        return runDatabaseCommand(std::vector<std::string>(argv + 1, argv + argc), std::cout, std::cerr);
    }

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
        if (ref_is_dir && args.serial_number) {
            std::cerr << "Error: --serial identifies one board and cannot be used in batch mode.\n";
            return 64;
        }

        PersistenceSession session = makePersistenceSession(args, config);

        if (ref_is_dir) {
            return runBatch(args, config, session);
        }

        const std::string inspection_id = generateInspectionId();
        const InspectionOutcome outcome = runInspection(
            config, args.reference_path, args.inspected_path, args.output_dir, inspection_id);
        const std::optional<std::int64_t> db_id = persistOutcome(
            session, config, inspection_id, args.reference_path, args.inspected_path, outcome);
        const std::string perf_path =
            (fs::path(args.output_dir) / (inspection_id + "_performance_report.md")).string();
        writePerformanceReport(perf_path, inspection_id, {toRecord(inspection_id, outcome, config.decision)}, config);
        printSingleSummary(inspection_id, args.reference_path, args.inspected_path, outcome);
        std::cout << "Performance   : " << perf_path << "\n";
        if (db_id) {
            std::cout << "Database      : " << session.database_path << "\n";
            std::cout << "Database ID   : " << *db_id << "\n";
        }
        if (session.failures > 0) return 74;  // result printed above; persistence failed
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