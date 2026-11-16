#pragma once

#include "detection/DefectMeasurement.hpp"

#include <opencv2/core.hpp>
#include <vector>

namespace optinspect {

/// Extensible interface for defect detection strategies. Phase 1 provides
/// DefectDetector (traditional difference + contour based). Future phases
/// may add alternative implementations (e.g. a YOLO-based detector) behind
/// this same interface without requiring changes to the inspection
/// pipeline or application layer.
class IDefectDetector {
public:
    virtual ~IDefectDetector() = default;

    /// Detects suspicious regions given an aligned reference/inspected pair.
    /// Both images must already be the same size (post-alignment).
    virtual std::vector<DefectMeasurement> detect(const cv::Mat& reference,
                                                    const cv::Mat& inspected) const = 0;
};

}  // namespace optinspect
