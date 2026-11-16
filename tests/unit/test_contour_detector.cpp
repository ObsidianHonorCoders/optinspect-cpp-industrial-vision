#include <gtest/gtest.h>

#include "common/Errors.hpp"
#include "detection/ContourDetector.hpp"

using namespace optinspect;

TEST(ContourDetectorTest, FindsSingleRectangularRegion) {
    cv::Mat mask = cv::Mat::zeros(50, 50, CV_8UC1);
    mask(cv::Rect(10, 10, 20, 15)).setTo(255);

    ContourDetector detector;
    auto candidates = detector.detect(mask);

    ASSERT_EQ(candidates.size(), 1u);
    // cv::contourArea on the pixel-boundary contour slightly underestimates
    // the filled rectangle's pixel count; allow a wider tolerance band.
    EXPECT_NEAR(candidates[0].measurement.area_px, 20 * 15, 40.0);
}

TEST(ContourDetectorTest, FindsMultipleSeparateRegions) {
    cv::Mat mask = cv::Mat::zeros(50, 50, CV_8UC1);
    mask(cv::Rect(0, 0, 5, 5)).setTo(255);
    mask(cv::Rect(40, 40, 5, 5)).setTo(255);

    ContourDetector detector;
    auto candidates = detector.detect(mask);

    EXPECT_EQ(candidates.size(), 2u);
}

TEST(ContourDetectorTest, ReturnsEmptyForBlankMask) {
    cv::Mat mask = cv::Mat::zeros(50, 50, CV_8UC1);
    ContourDetector detector;
    auto candidates = detector.detect(mask);
    EXPECT_TRUE(candidates.empty());
}

TEST(ContourDetectorTest, ThrowsOnEmptyImage) {
    ContourDetector detector;
    cv::Mat empty;
    EXPECT_THROW(detector.detect(empty), OptInspectError);
}
