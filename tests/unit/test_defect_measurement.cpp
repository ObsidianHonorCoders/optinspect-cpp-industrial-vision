#include <gtest/gtest.h>

#include "detection/DefectMeasurement.hpp"

using namespace optinspect;

TEST(DefectMeasurementTest, ComputesBoundingBoxAndAreaForSquare) {
    std::vector<cv::Point> square = {{10, 10}, {30, 10}, {30, 30}, {10, 30}};
    DefectMeasurement m = computeMeasurement(square);

    // cv::boundingRect treats contour points as inclusive pixel coordinates,
    // so the bounding box spans one pixel beyond the max coordinate in each
    // dimension (21x21 for points ranging from 10 to 30).
    EXPECT_EQ(m.bounding_box, cv::Rect(10, 10, 21, 21));
    EXPECT_NEAR(m.area_px, 400.0, 1.0);
    EXPECT_EQ(m.width, 21);
    EXPECT_EQ(m.height, 21);
    EXPECT_NEAR(m.aspect_ratio, 1.0, 1e-6);
}

TEST(DefectMeasurementTest, ComputesCentroidNearGeometricCenter) {
    std::vector<cv::Point> square = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    DefectMeasurement m = computeMeasurement(square);
    EXPECT_NEAR(m.centroid.x, 5.0, 1.5);
    EXPECT_NEAR(m.centroid.y, 5.0, 1.5);
}

TEST(DefectMeasurementTest, AspectRatioForWideRectangle) {
    std::vector<cv::Point> rect = {{0, 0}, {40, 0}, {40, 10}, {0, 10}};
    DefectMeasurement m = computeMeasurement(rect);
    // Bounding box is 41x11 (cv::boundingRect is inclusive of max coordinate).
    EXPECT_NEAR(m.aspect_ratio, 41.0 / 11.0, 1e-6);
}

TEST(DefectMeasurementTest, ConfidenceIncreasesWithArea) {
    std::vector<cv::Point> small = {{0, 0}, {5, 0}, {5, 5}, {0, 5}};
    std::vector<cv::Point> large = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};

    DefectMeasurement small_m = computeMeasurement(small);
    DefectMeasurement large_m = computeMeasurement(large);

    EXPECT_LT(small_m.confidence, large_m.confidence);
    EXPECT_LE(large_m.confidence, 1.0);
}
