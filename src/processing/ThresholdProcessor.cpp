#include "processing/ThresholdProcessor.hpp"

#include <opencv2/imgproc.hpp>

#include "common/Errors.hpp"

namespace optinspect {

namespace {
int ensureOdd(int value) {
    int v = value < 3 ? 3 : value;
    return (v % 2 == 0) ? v + 1 : v;
}
}  // namespace

ThresholdProcessor::ThresholdProcessor(ThresholdConfig config) : config_(config) {}

cv::Mat ThresholdProcessor::apply(const cv::Mat& image) const {
    if (image.empty() || image.channels() != 1) {
        throw OptInspectError("ThresholdProcessor::apply requires a non-empty single-channel image.");
    }

    cv::Mat mask;
    switch (config_.method) {
        case ThresholdMethod::FIXED:
            cv::threshold(image, mask, config_.fixed_threshold, 255, cv::THRESH_BINARY);
            break;
        case ThresholdMethod::OTSU:
            cv::threshold(image, mask, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);
            break;
        case ThresholdMethod::ADAPTIVE_MEAN:
            cv::adaptiveThreshold(image, mask, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY,
                                   ensureOdd(config_.adaptive_block_size), config_.adaptive_c);
            break;
        case ThresholdMethod::ADAPTIVE_GAUSSIAN:
            cv::adaptiveThreshold(image, mask, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY,
                                   ensureOdd(config_.adaptive_block_size), config_.adaptive_c);
            break;
    }
    return mask;
}

}  // namespace optinspect
