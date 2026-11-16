#pragma once

#include <opencv2/core.hpp>

namespace optinspect {

/// Geometric and shape measurements for a single detected suspicious
/// region, computed directly from its contour. These are descriptive
/// measurements only; OptiInspect Phase 1 does not classify defects into
/// the six DeepPCB semantic categories (open, short, mousebite, spur,
/// copper, pin-hole) because traditional contour-based analysis cannot
/// reliably distinguish between them without additional domain-specific
/// rules or trained models.
struct DefectMeasurement {
    cv::Rect bounding_box;
    double area_px = 0.0;
    cv::Point2d centroid{0.0, 0.0};
    int width = 0;
    int height = 0;
    double perimeter_px = 0.0;
    double aspect_ratio = 0.0;  ///< width / height (height clamped to >= 1).

    /// Rule-based severity score in [0,1], derived from area relative to
    /// the configured maximum; NOT a semantic classification confidence.
    /// Larger, more solid regions score higher. This expresses "how
    /// confident we are that this is a meaningful defect region" based on
    /// size and shape, not "what kind of defect this is."
    double confidence = 0.0;
};

/// Computes DefectMeasurement from a contour, given the image area used to
/// derive confidence scaling.
DefectMeasurement computeMeasurement(const std::vector<cv::Point>& contour);

}  // namespace optinspect
