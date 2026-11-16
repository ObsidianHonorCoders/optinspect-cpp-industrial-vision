#include "detection/DefectDetector.hpp"

#include "detection/ContourDetector.hpp"

namespace optinspect {

DefectDetector::DefectDetector(PreprocessingConfig preprocessing, ThresholdConfig threshold,
                                MorphologyConfig morphology, DetectionConfig detection)
    : comparator_(preprocessing, threshold, morphology), detection_(detection) {}

std::vector<DefectMeasurement> DefectDetector::detect(const cv::Mat& reference,
                                                        const cv::Mat& inspected) const {
    last_comparison_ = comparator_.compare(reference, inspected);

    const ContourDetector contour_detector;
    const auto candidates = contour_detector.detect(last_comparison_.binary_mask);

    std::vector<DefectMeasurement> filtered;
    filtered.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const double area = candidate.measurement.area_px;
        if (area < detection_.min_defect_area_px || area > detection_.max_defect_area_px) {
            continue;
        }
        if (candidate.measurement.confidence < detection_.min_confidence) {
            continue;
        }
        filtered.push_back(candidate.measurement);
    }
    return filtered;
}

}  // namespace optinspect
