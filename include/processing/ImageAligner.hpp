#pragma once

#include "common/Config.hpp"

#include <opencv2/core.hpp>

namespace optinspect {

/// Result of an alignment attempt between a reference and inspected image.
struct AlignmentResult {
    cv::Mat aligned_image;   ///< Inspected image warped into the reference frame.
    bool success = false;    ///< Whether alignment converged to a usable transform.
    /// Normalized confidence in [0,1]: for ECC this is the correlation
    /// coefficient (already bounded in [0,1] for well-posed problems); for
    /// ORB it is the fraction of matches classified as RANSAC inliers.
    double confidence = 0.0;
    std::string message;     ///< Human-readable status/diagnostic message.
};

/// Aligns an inspected image to a reference image so that pixel-wise
/// comparison is valid. Two traditional registration strategies are
/// supported (see AlignmentMethod): ECC (dense, intensity-based) and ORB
/// (sparse, feature-based). Both report failure explicitly rather than
/// silently returning an unaligned image.
class ImageAligner {
public:
    explicit ImageAligner(AlignmentConfig config);

    /// Aligns `inspected` onto `reference`. Both images must be non-empty
    /// and have the same number of channels; this function handles
    /// grayscale conversion internally for the registration search but
    /// warps and returns the original (color) inspected image.
    AlignmentResult align(const cv::Mat& reference, const cv::Mat& inspected) const;

private:
    AlignmentResult alignEcc(const cv::Mat& reference, const cv::Mat& inspected) const;
    AlignmentResult alignOrb(const cv::Mat& reference, const cv::Mat& inspected) const;

    AlignmentConfig config_;
};

}  // namespace optinspect
