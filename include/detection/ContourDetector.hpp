#pragma once

#include "detection/DefectMeasurement.hpp"

#include <opencv2/core.hpp>
#include <vector>

namespace optinspect {

/// A candidate suspicious region produced during contour extraction, before
/// final filtering. Carries both the raw contour (useful for drawing
/// annotations) and its derived DefectMeasurement.
struct DefectCandidate {
    std::vector<cv::Point> contour;
    DefectMeasurement measurement;
};

/// Extracts external contours from a binary mask. A thin wrapper over
/// cv::findContours kept as its own class so the detection pipeline has a
/// single, named, testable seam for "how do we get candidate regions from
/// a mask", independent of how the mask itself was produced.
class ContourDetector {
public:
    /// Finds external contours in `binary_mask` (expected to be an 8-bit
    /// single-channel image with 0/255 values) and returns one
    /// DefectCandidate per contour, with measurements already computed.
    std::vector<DefectCandidate> detect(const cv::Mat& binary_mask) const;
};

}  // namespace optinspect
