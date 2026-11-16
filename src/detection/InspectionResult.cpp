#include "detection/InspectionResult.hpp"

namespace optinspect {

std::string toString(InspectionDecision decision) {
    switch (decision) {
        case InspectionDecision::PASS:
            return "PASS";
        case InspectionDecision::FAIL:
            return "FAIL";
        case InspectionDecision::REVIEW:
            return "REVIEW";
    }
    return "UNKNOWN";
}

}  // namespace optinspect
