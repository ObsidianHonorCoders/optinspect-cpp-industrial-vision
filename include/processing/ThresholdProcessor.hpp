#pragma once

#include "common/Config.hpp"

#include <opencv2/core.hpp>

namespace optinspect {

/// Converts a single-channel grayscale (typically a difference) image into
/// a binary mask using the configured thresholding strategy.
class ThresholdProcessor {
public:
    explicit ThresholdProcessor(ThresholdConfig config);

    /// Applies thresholding and returns a binary (0/255) mask of the same
    /// size as `image`. `image` must be single-channel 8-bit.
    cv::Mat apply(const cv::Mat& image) const;

private:
    ThresholdConfig config_;
};

}  // namespace optinspect
