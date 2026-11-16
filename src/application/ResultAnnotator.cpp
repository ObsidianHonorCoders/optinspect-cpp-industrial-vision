#include "ResultAnnotator.hpp"

#include <opencv2/imgproc.hpp>

namespace optinspect {

cv::Mat ResultAnnotator::annotate(const cv::Mat& inspected, const InspectionResult& result) {
    cv::Mat annotated;
    if (inspected.channels() == 1) {
        cv::cvtColor(inspected, annotated, cv::COLOR_GRAY2BGR);
    } else {
        annotated = inspected.clone();
    }

    const cv::Scalar box_color = (result.decision == InspectionDecision::FAIL)
                                      ? cv::Scalar(0, 0, 255)      // red
                                      : cv::Scalar(0, 165, 255);   // orange (REVIEW) / informational

    for (const auto& defect : result.defects) {
        cv::rectangle(annotated, defect.bounding_box, box_color, 2);
        const std::string label = "A=" + std::to_string(static_cast<int>(defect.area_px));
        const cv::Point text_origin(defect.bounding_box.x, std::max(0, defect.bounding_box.y - 5));
        cv::putText(annotated, label, text_origin, cv::FONT_HERSHEY_SIMPLEX, 0.4, box_color, 1);
    }

    const std::string banner = "Decision: " + toString(result.decision);
    const cv::Scalar banner_color = (result.decision == InspectionDecision::PASS)
                                         ? cv::Scalar(0, 200, 0)
                                         : (result.decision == InspectionDecision::FAIL)
                                               ? cv::Scalar(0, 0, 255)
                                               : cv::Scalar(0, 165, 255);
    cv::putText(annotated, banner, cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.8, banner_color, 2);

    return annotated;
}

}  // namespace optinspect
