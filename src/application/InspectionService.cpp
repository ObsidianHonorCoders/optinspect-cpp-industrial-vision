#include "InspectionService.hpp"

#include "ReportWriter.hpp"
#include "ResultAnnotator.hpp"

#include "acquisition/FileImageSource.hpp"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace optinspect {

namespace fs = std::filesystem;

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

std::string absolutePath(const std::string& path) {
    return fs::absolute(fs::path(path)).lexically_normal().string();
}

InspectionOutcome runInspection(const InspectionConfig& config, const std::string& reference_path,
                                const std::string& inspected_path, const std::string& output_dir,
                                const std::string& inspection_id, const StageObserver& observer) {
    Stopwatch total_timer;
    if (observer) observer("loading");

    const cv::Mat reference = FileImageSource(reference_path).load();
    const cv::Mat inspected = FileImageSource(inspected_path).load();

    const InspectionPipeline pipeline(config);
    const PipelineOutput output = pipeline.run(reference, inspected, observer);

    if (observer) observer("writing");
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

InspectionArtifacts makeArtifacts(const std::string& inspection_id,
                                  const std::string& reference_path,
                                  const std::string& inspected_path,
                                  const InspectionOutcome& outcome) {
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
    return artifacts;
}

}  // namespace optinspect
