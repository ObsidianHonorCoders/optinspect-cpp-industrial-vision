#pragma once

#include <opencv2/core.hpp>

namespace optinspect {

/// Collection of small, stateless, reusable OpenCV-based image processing
/// operations shared across the pipeline (color conversion, blurring,
/// difference, normalization, ROI cropping). Kept as static utility
/// functions rather than an object with state, since none of these
/// operations require configuration beyond their explicit parameters.
class ImageProcessor {
public:
    /// Converts a BGR image to single-channel grayscale. No-op (returns a
    /// clone) if the image is already single-channel.
    static cv::Mat toGrayscale(const cv::Mat& image);

    /// Applies a Gaussian blur with an odd kernel size (kernel_size is
    /// rounded up to the next odd value if given an even number).
    static cv::Mat gaussianBlur(const cv::Mat& image, int kernel_size, double sigma);

    /// Computes the per-pixel absolute difference between two images of
    /// identical size and type.
    static cv::Mat absoluteDifference(const cv::Mat& a, const cv::Mat& b);

    /// Normalizes a single-channel image to the full 0-255 range using
    /// min-max normalization. Useful for visualizing low-contrast
    /// difference images.
    static cv::Mat normalizeTo8U(const cv::Mat& image);

    /// Returns the sub-image within the given rectangle, clamped to the
    /// image bounds. Returns the full image if the ROI is empty.
    static cv::Mat cropToRoi(const cv::Mat& image, const cv::Rect& roi);
};

}  // namespace optinspect
