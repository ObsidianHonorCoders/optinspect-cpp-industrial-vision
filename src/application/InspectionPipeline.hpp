#pragma once

#include "common/Config.hpp"
#include "common/Timing.hpp"
#include "detection/InspectionResult.hpp"

#include <opencv2/core.hpp>

#include <functional>
#include <string>

namespace optinspect {

/// Output of a full pipeline run: the inspection result plus the
/// intermediate images needed for saving annotated/difference outputs.
struct PipelineOutput {
    InspectionResult result;
    cv::Mat aligned_inspected;
    cv::Mat difference_image;
    cv::Mat binary_mask;
    PipelineTimer timer;
};

/// Optional notification invoked on the calling thread right before each
/// pipeline stage starts ("alignment", "detection", "decision"). It lets a
/// caller (e.g. the GUI worker) report real progress; the pipeline behaves
/// identically with or without it.
using StageObserver = std::function<void(const std::string& stage_name)>;

/// Orchestrates the full inspection pipeline: alignment, comparison,
/// detection, and decision. This class contains no CLI or file I/O
/// concerns (those belong to the application's main()); it operates purely
/// on in-memory images and configuration, which keeps it independently
/// unit/integration-testable.
class InspectionPipeline {
public:
    explicit InspectionPipeline(InspectionConfig config);

    PipelineOutput run(const cv::Mat& reference, const cv::Mat& inspected,
                       const StageObserver& observer = {}) const;

private:
    InspectionConfig config_;
};

}  // namespace optinspect
