#include "processing/ImageProcessor.hpp"

#include <opencv2/imgproc.hpp>

namespace optinspect {

cv::Mat ImageProcessor::toGrayscale(const cv::Mat& image) {
    if (image.channels() == 1) {
        return image.clone();
    }
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

cv::Mat ImageProcessor::gaussianBlur(const cv::Mat& image, int kernel_size, double sigma) {
    int k = kernel_size < 1 ? 1 : kernel_size;
    if (k % 2 == 0) {
        ++k;  // OpenCV requires an odd kernel size.
    }
    cv::Mat blurred;
    cv::GaussianBlur(image, blurred, cv::Size(k, k), sigma);
    return blurred;
}

cv::Mat ImageProcessor::absoluteDifference(const cv::Mat& a, const cv::Mat& b) {
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    return diff;
}

cv::Mat ImageProcessor::normalizeTo8U(const cv::Mat& image) {
    cv::Mat normalized;
    cv::normalize(image, normalized, 0, 255, cv::NORM_MINMAX, CV_8U);
    return normalized;
}

cv::Mat ImageProcessor::cropToRoi(const cv::Mat& image, const cv::Rect& roi) {
    if (roi.width <= 0 || roi.height <= 0) {
        return image;
    }
    const cv::Rect bounds(0, 0, image.cols, image.rows);
    const cv::Rect clamped = roi & bounds;
    if (clamped.width <= 0 || clamped.height <= 0) {
        return image;
    }
    return image(clamped).clone();
}

}  // namespace optinspect
