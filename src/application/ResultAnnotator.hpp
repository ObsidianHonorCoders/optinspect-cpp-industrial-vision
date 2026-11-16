#pragma once

#include "detection/InspectionResult.hpp"

#include <opencv2/core.hpp>

namespace optinspect {

/// Draws bounding boxes and summary annotations for detected defects onto
/// a copy of the inspected image. This is a small presentation helper
/// local to the application layer (not the reserved "visualization" module,
/// which is out of scope for Phase 1 and intended for a future
/// OpenGL/GUI-based viewer).
class ResultAnnotator {
public:
    /// Returns a BGR copy of `inspected` with bounding boxes drawn for each
    /// defect in `result.defects`, color-coded by decision outcome.
    static cv::Mat annotate(const cv::Mat& inspected, const InspectionResult& result);
};

}  // namespace optinspect
