#pragma once

#include "common/Config.hpp"
#include "detection/InspectionResult.hpp"
#include "processing/ImageAligner.hpp"

#include <vector>

namespace optinspect {

/// Applies the configured PASS/FAIL/REVIEW decision rules to a completed
/// detection pass. Kept separate from DefectDetector and ImageAligner so
/// decision thresholds can be changed or extended without touching image
/// processing or detection logic (single responsibility).
class InspectionDecider {
public:
    explicit InspectionDecider(DecisionConfig config);

    /// `alignment` carries whether registration succeeded and at what
    /// confidence; `defects` is the filtered list of accepted defect
    /// measurements from DefectDetector.
    InspectionResult decide(const AlignmentResult& alignment,
                             const std::vector<DefectMeasurement>& defects) const;

private:
    DecisionConfig config_;
};

}  // namespace optinspect
