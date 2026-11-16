#include "detection/DefectMeasurement.hpp"

#include <opencv2/imgproc.hpp>

namespace optinspect {

DefectMeasurement computeMeasurement(const std::vector<cv::Point>& contour) {
    DefectMeasurement m;
    m.bounding_box = cv::boundingRect(contour);
    m.area_px = cv::contourArea(contour);
    m.perimeter_px = cv::arcLength(contour, true);
    m.width = m.bounding_box.width;
    m.height = m.bounding_box.height;

    const int safe_height = std::max(1, m.height);
    m.aspect_ratio = static_cast<double>(m.width) / static_cast<double>(safe_height);

    const cv::Moments moments = cv::moments(contour);
    if (moments.m00 != 0.0) {
        m.centroid = cv::Point2d(moments.m10 / moments.m00, moments.m01 / moments.m00);
    } else {
        // Degenerate contour (e.g. a single line of points): fall back to
        // the bounding box center.
        m.centroid = cv::Point2d(m.bounding_box.x + m.bounding_box.width / 2.0,
                                  m.bounding_box.y + m.bounding_box.height / 2.0);
    }

    // Confidence is intentionally a simple, explainable rule-based score:
    // larger regions are scored higher up to a saturation point, reflecting
    // greater certainty that the region is a genuine defect rather than
    // image noise. This is NOT a semantic classification confidence.
    constexpr double kSaturationArea = 2000.0;
    m.confidence = std::min(1.0, m.area_px / kSaturationArea);

    return m;
}

}  // namespace optinspect
