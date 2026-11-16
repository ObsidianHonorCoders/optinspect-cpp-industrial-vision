#include "detection/InspectionDecider.hpp"

namespace optinspect {

InspectionDecider::InspectionDecider(DecisionConfig config) : config_(config) {}

InspectionResult InspectionDecider::decide(const AlignmentResult& alignment,
                                            const std::vector<DefectMeasurement>& defects) const {
    InspectionResult result;
    result.defects = defects;
    result.alignment_succeeded = alignment.success;
    result.alignment_confidence = alignment.confidence;
    result.alignment_message = alignment.message;

    if (!alignment.success) {
        result.decision = InspectionDecision::REVIEW;
        result.decision_reason = "Image alignment failed: " + alignment.message;
        return result;
    }

    if (alignment.confidence < config_.min_alignment_confidence) {
        result.decision = InspectionDecision::REVIEW;
        result.decision_reason = "Alignment confidence (" + std::to_string(alignment.confidence) +
                                  ") below configured minimum (" +
                                  std::to_string(config_.min_alignment_confidence) +
                                  "); comparison is unreliable.";
        return result;
    }

    double max_area = 0.0;
    for (const auto& defect : defects) {
        max_area = std::max(max_area, defect.area_px);
    }

    if (max_area >= config_.fail_area_px) {
        result.decision = InspectionDecision::FAIL;
        result.decision_reason = "At least one defect region (" + std::to_string(max_area) +
                                  " px) exceeds the configured failure area (" +
                                  std::to_string(config_.fail_area_px) + " px).";
        return result;
    }

    if (static_cast<int>(defects.size()) >= config_.fail_defect_count_threshold) {
        result.decision = InspectionDecision::FAIL;
        result.decision_reason = "Detected defect count (" + std::to_string(defects.size()) +
                                  ") meets or exceeds the configured failure threshold (" +
                                  std::to_string(config_.fail_defect_count_threshold) + ").";
        return result;
    }

    result.decision = InspectionDecision::PASS;
    result.decision_reason = "No defects met the configured failure criteria.";
    return result;
}

}  // namespace optinspect
