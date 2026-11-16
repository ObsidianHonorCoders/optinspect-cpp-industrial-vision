#pragma once

#include "common/Config.hpp"

#include <opencv2/core.hpp>

namespace optinspect {

/// Result of comparing an aligned reference/inspected image pair.
struct ComparisonResult {
    cv::Mat difference_image;  ///< 8-bit single-channel absolute difference (grayscale).
    cv::Mat binary_mask;       ///< Thresholded + morphologically cleaned binary mask.
};

/// Produces the preprocessed difference image and binary defect mask used
/// as input to contour-based defect detection. This class owns the
/// grayscale conversion, blurring, differencing, thresholding, and
/// morphological cleanup steps so DefectDetector only has to deal with
/// contours and measurements.
class ImageComparator {
public:
    ImageComparator(PreprocessingConfig preprocessing, ThresholdConfig threshold,
                     MorphologyConfig morphology);

    /// Both images must be the same size (post-alignment) and will be
    /// converted to grayscale internally if not already.
    ComparisonResult compare(const cv::Mat& reference, const cv::Mat& inspected) const;

private:
    PreprocessingConfig preprocessing_;
    ThresholdConfig threshold_;
    MorphologyConfig morphology_;
};

}  // namespace optinspect
