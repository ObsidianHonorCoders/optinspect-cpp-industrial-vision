#include "ReportWriter.hpp"

#include "third_party/nlohmann/json.hpp"

#include <fstream>

namespace optinspect {

using json = nlohmann::json;

namespace {

json thresholdMethodToJson(ThresholdMethod method) {
    switch (method) {
        case ThresholdMethod::FIXED: return "FIXED";
        case ThresholdMethod::OTSU: return "OTSU";
        case ThresholdMethod::ADAPTIVE_MEAN: return "ADAPTIVE_MEAN";
        case ThresholdMethod::ADAPTIVE_GAUSSIAN: return "ADAPTIVE_GAUSSIAN";
    }
    return "UNKNOWN";
}

json alignmentMethodToJson(AlignmentMethod method) {
    switch (method) {
        case AlignmentMethod::ECC: return "ECC";
        case AlignmentMethod::ORB: return "ORB";
        case AlignmentMethod::NONE: return "NONE";
    }
    return "UNKNOWN";
}

json configurationToJson(const InspectionConfig& config) {
    return {
        {"preprocessing",
         {{"gaussian_kernel_size", config.preprocessing.gaussian_kernel_size},
          {"gaussian_sigma", config.preprocessing.gaussian_sigma}}},
        {"alignment",
         {{"method", alignmentMethodToJson(config.alignment.method)},
          {"max_translation_px", config.alignment.max_translation_px}}},
        {"threshold",
         {{"method", thresholdMethodToJson(config.threshold.method)},
          {"fixed_threshold", config.threshold.fixed_threshold}}},
        {"morphology",
         {{"kernel_size", config.morphology.kernel_size},
          {"close_iterations", config.morphology.close_iterations},
          {"open_iterations", config.morphology.open_iterations}}},
        {"detection",
         {{"min_defect_area_px", config.detection.min_defect_area_px},
          {"max_defect_area_px", config.detection.max_defect_area_px}}},
        {"decision",
         {{"fail_defect_count_threshold", config.decision.fail_defect_count_threshold},
          {"fail_area_px", config.decision.fail_area_px},
          {"min_alignment_confidence", config.decision.min_alignment_confidence}}}};
}
}  // namespace

std::string ReportWriter::configurationJson(const InspectionConfig& config) {
    return configurationToJson(config).dump();
}

void ReportWriter::writeJson(const std::string& output_path, const InspectionMetadata& metadata,
                              const InspectionResult& result, const PipelineTimer& timer,
                              const InspectionConfig& config) {
    json report;

    report["inspection_id"] = metadata.inspection_id;
    report["reference_image"] = metadata.reference_path;
    report["inspected_image"] = metadata.inspected_path;
    report["reference_dimensions"] = {{"width", metadata.reference_size.width},
                                       {"height", metadata.reference_size.height}};
    report["inspected_dimensions"] = {{"width", metadata.inspected_size.width},
                                       {"height", metadata.inspected_size.height}};

    report["alignment"] = {{"succeeded", result.alignment_succeeded},
                            {"confidence", result.alignment_confidence},
                            {"message", result.alignment_message},
                            {"method", alignmentMethodToJson(config.alignment.method)}};

    report["decision"] = toString(result.decision);
    report["decision_reason"] = result.decision_reason;
    report["defect_count"] = result.defects.size();

    json defects_json = json::array();
    for (const auto& defect : result.defects) {
        json d;
        d["bounding_box"] = {{"x", defect.bounding_box.x},
                             {"y", defect.bounding_box.y},
                             {"width", defect.bounding_box.width},
                             {"height", defect.bounding_box.height}};
        d["area_px"] = defect.area_px;
        d["centroid"] = {{"x", defect.centroid.x}, {"y", defect.centroid.y}};
        d["width"] = defect.width;
        d["height"] = defect.height;
        d["perimeter_px"] = defect.perimeter_px;
        d["aspect_ratio"] = defect.aspect_ratio;
        d["confidence"] = defect.confidence;
        defects_json.push_back(d);
    }
    report["defects"] = defects_json;

    json timings_json = json::array();
    for (const auto& t : timer.timings()) {
        timings_json.push_back({{"stage", t.stage_name}, {"milliseconds", t.milliseconds}});
    }
    report["stage_timings_ms"] = timings_json;
    report["total_processing_time_ms"] = timer.totalMilliseconds();

    report["configuration"] = configurationToJson(config);

    std::ofstream out(output_path);
    out << report.dump(2);
}

}  // namespace optinspect
