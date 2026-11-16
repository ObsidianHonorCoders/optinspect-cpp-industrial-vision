#include "detection/ContourDetector.hpp"

#include <opencv2/imgproc.hpp>

#include "common/Errors.hpp"

namespace optinspect {

std::vector<DefectCandidate> ContourDetector::detect(const cv::Mat& binary_mask) const {
    if (binary_mask.empty() || binary_mask.channels() != 1) {
        throw OptInspectError("ContourDetector::detect requires a non-empty single-channel mask.");
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<DefectCandidate> candidates;
    candidates.reserve(contours.size());
    for (auto& contour : contours) {
        DefectCandidate candidate;
        candidate.measurement = computeMeasurement(contour);
        candidate.contour = std::move(contour);
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

}  // namespace optinspect
