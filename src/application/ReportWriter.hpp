#pragma once

#include "common/Config.hpp"
#include "common/Timing.hpp"
#include "detection/InspectionResult.hpp"

#include <opencv2/core.hpp>

#include <string>

namespace optinspect {

/// Metadata describing one inspection run, independent of the pixel
/// results, used to populate the structured JSON report.
struct InspectionMetadata {
    std::string inspection_id;
    std::string reference_path;
    std::string inspected_path;
    cv::Size reference_size;
    cv::Size inspected_size;
};

/// Serializes an InspectionResult, its metadata, stage timings, and the
/// relevant configuration into a structured JSON report file.
class ReportWriter {
public:
    static void writeJson(const std::string& output_path, const InspectionMetadata& metadata,
                           const InspectionResult& result, const PipelineTimer& timer,
                           const InspectionConfig& config);
};

}  // namespace optinspect
