#pragma once

#include "detection/DefectMeasurement.hpp"

#include <string>
#include <vector>

namespace optinspect {

enum class InspectionDecision { PASS, FAIL, REVIEW };

std::string toString(InspectionDecision decision);

/// Aggregated result of a complete inspection run: the final decision plus
/// every defect measurement that survived filtering, along with the
/// reasoning that led to the decision.
struct InspectionResult {
    InspectionDecision decision = InspectionDecision::REVIEW;
    std::string decision_reason;
    std::vector<DefectMeasurement> defects;
    bool alignment_succeeded = false;
    double alignment_confidence = 0.0;
    std::string alignment_message;
};

}  // namespace optinspect
