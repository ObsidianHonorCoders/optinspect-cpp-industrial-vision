#include "database/Models.hpp"

#include <stdexcept>

namespace optinspect::database {

std::string toString(InspectionStatus status) {
    switch (status) {
        case InspectionStatus::PASS: return "PASS";
        case InspectionStatus::FAIL: return "FAIL";
        case InspectionStatus::REVIEW: return "REVIEW";
    }
    return "UNKNOWN";
}

std::string toString(AlignmentStatus status) {
    switch (status) {
        case AlignmentStatus::SUCCEEDED: return "SUCCEEDED";
        case AlignmentStatus::FAILED: return "FAILED";
    }
    return "UNKNOWN";
}

std::string toString(ImageType type) {
    switch (type) {
        case ImageType::REFERENCE: return "REFERENCE";
        case ImageType::INSPECTED: return "INSPECTED";
        case ImageType::ANNOTATED: return "ANNOTATED";
        case ImageType::DIFFERENCE: return "DIFFERENCE";
    }
    return "UNKNOWN";
}

InspectionStatus parseInspectionStatus(const std::string& text) {
    if (text == "PASS") return InspectionStatus::PASS;
    if (text == "FAIL") return InspectionStatus::FAIL;
    if (text == "REVIEW") return InspectionStatus::REVIEW;
    throw std::invalid_argument("Invalid inspection status '" + text +
                                "' (expected PASS, FAIL or REVIEW).");
}

AlignmentStatus parseAlignmentStatus(const std::string& text) {
    if (text == "SUCCEEDED") return AlignmentStatus::SUCCEEDED;
    if (text == "FAILED") return AlignmentStatus::FAILED;
    throw std::invalid_argument("Invalid alignment status '" + text + "'.");
}

ImageType parseImageType(const std::string& text) {
    if (text == "REFERENCE") return ImageType::REFERENCE;
    if (text == "INSPECTED") return ImageType::INSPECTED;
    if (text == "ANNOTATED") return ImageType::ANNOTATED;
    if (text == "DIFFERENCE") return ImageType::DIFFERENCE;
    throw std::invalid_argument("Invalid image type '" + text + "'.");
}

}  // namespace optinspect::database
