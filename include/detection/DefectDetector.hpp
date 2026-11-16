#pragma once

#include "common/Config.hpp"
#include "detection/IDefectDetector.hpp"
#include "processing/ImageComparator.hpp"

#include <opencv2/core.hpp>

namespace optinspect {

/// Traditional computer-vision defect detector: builds a difference image
/// between the reference and inspected images, thresholds and cleans it
/// morphologically, extracts external contours, computes geometric
/// measurements for each, and filters candidates by configurable area
/// rules (DetectionConfig).
///
/// This detector reports suspicious regions based on pixel-level
/// differences and shape heuristics. It does not attempt to classify
/// defects into the six DeepPCB semantic categories (open, short,
/// mousebite, spur, copper, pin-hole); doing so reliably would require
/// either defect-specific geometric rules validated against ground truth,
/// or a learned classifier, which is out of scope for Phase 1.
class DefectDetector final : public IDefectDetector {
public:
    DefectDetector(PreprocessingConfig preprocessing, ThresholdConfig threshold,
                   MorphologyConfig morphology, DetectionConfig detection);

    std::vector<DefectMeasurement> detect(const cv::Mat& reference,
                                           const cv::Mat& inspected) const override;

    /// Returns the last comparison result (difference image + binary mask)
    /// computed during detect(), useful for diagnostics and output
    /// generation without recomputing it.
    const ComparisonResult& lastComparison() const { return last_comparison_; }

private:
    ImageComparator comparator_;
    DetectionConfig detection_;
    mutable ComparisonResult last_comparison_;
};

}  // namespace optinspect
