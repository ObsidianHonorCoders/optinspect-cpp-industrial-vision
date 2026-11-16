#pragma once

#include "common/Config.hpp"

#include <opencv2/core.hpp>

namespace optinspect {

/// Applies morphological operations (erosion, dilation, opening, closing)
/// to a binary mask in order to remove isolated noise pixels and
/// consolidate fragmented defect regions into coherent blobs before
/// contour extraction.
class MorphologicalProcessor {
public:
    explicit MorphologicalProcessor(MorphologyConfig config);

    /// Applies an opening pass (erode then dilate) to remove small noise
    /// specks, followed by a closing pass (dilate then erode) to fill small
    /// holes and connect nearby defect fragments.
    cv::Mat clean(const cv::Mat& binary_mask) const;

    cv::Mat erode(const cv::Mat& binary_mask, int iterations = 1) const;
    cv::Mat dilate(const cv::Mat& binary_mask, int iterations = 1) const;
    cv::Mat open(const cv::Mat& binary_mask, int iterations = 1) const;
    cv::Mat close(const cv::Mat& binary_mask, int iterations = 1) const;

private:
    MorphologyConfig config_;
    cv::Mat kernel_;
};

}  // namespace optinspect
