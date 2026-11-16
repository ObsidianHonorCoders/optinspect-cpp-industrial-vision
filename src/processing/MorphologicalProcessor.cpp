#include "processing/MorphologicalProcessor.hpp"

#include <opencv2/imgproc.hpp>

namespace optinspect {

namespace {
int ensureOdd(int value) {
    int v = value < 1 ? 1 : value;
    return (v % 2 == 0) ? v + 1 : v;
}
}  // namespace

MorphologicalProcessor::MorphologicalProcessor(MorphologyConfig config) : config_(config) {
    const int k = ensureOdd(config_.kernel_size);
    kernel_ = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
}

cv::Mat MorphologicalProcessor::erode(const cv::Mat& binary_mask, int iterations) const {
    cv::Mat result;
    cv::erode(binary_mask, result, kernel_, cv::Point(-1, -1), iterations);
    return result;
}

cv::Mat MorphologicalProcessor::dilate(const cv::Mat& binary_mask, int iterations) const {
    cv::Mat result;
    cv::dilate(binary_mask, result, kernel_, cv::Point(-1, -1), iterations);
    return result;
}

cv::Mat MorphologicalProcessor::open(const cv::Mat& binary_mask, int iterations) const {
    cv::Mat result;
    cv::morphologyEx(binary_mask, result, cv::MORPH_OPEN, kernel_, cv::Point(-1, -1), iterations);
    return result;
}

cv::Mat MorphologicalProcessor::close(const cv::Mat& binary_mask, int iterations) const {
    cv::Mat result;
    cv::morphologyEx(binary_mask, result, cv::MORPH_CLOSE, kernel_, cv::Point(-1, -1), iterations);
    return result;
}

cv::Mat MorphologicalProcessor::clean(const cv::Mat& binary_mask) const {
    cv::Mat opened = open(binary_mask, config_.open_iterations);
    cv::Mat closed = close(opened, config_.close_iterations);
    return closed;
}

}  // namespace optinspect
