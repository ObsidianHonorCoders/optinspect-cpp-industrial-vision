#include <gtest/gtest.h>

#include "processing/MorphologicalProcessor.hpp"

using namespace optinspect;

namespace {
MorphologyConfig defaultConfig() {
    MorphologyConfig config;
    config.kernel_size = 3;
    config.open_iterations = 1;
    config.close_iterations = 1;
    return config;
}
}  // namespace

TEST(MorphologicalProcessorTest, ErodeShrinksForegroundRegion) {
    MorphologicalProcessor processor(defaultConfig());
    cv::Mat mask = cv::Mat::zeros(20, 20, CV_8UC1);
    mask(cv::Rect(5, 5, 10, 10)).setTo(255);

    cv::Mat eroded = processor.erode(mask, 1);
    EXPECT_LE(cv::countNonZero(eroded), cv::countNonZero(mask));
}

TEST(MorphologicalProcessorTest, DilateGrowsForegroundRegion) {
    MorphologicalProcessor processor(defaultConfig());
    cv::Mat mask = cv::Mat::zeros(20, 20, CV_8UC1);
    mask(cv::Rect(5, 5, 10, 10)).setTo(255);

    cv::Mat dilated = processor.dilate(mask, 1);
    EXPECT_GE(cv::countNonZero(dilated), cv::countNonZero(mask));
}

TEST(MorphologicalProcessorTest, OpenRemovesIsolatedNoisePixels) {
    MorphologicalProcessor processor(defaultConfig());
    cv::Mat mask = cv::Mat::zeros(50, 50, CV_8UC1);
    mask.at<uchar>(5, 5) = 255;  // isolated single-pixel noise
    mask(cv::Rect(20, 20, 15, 15)).setTo(255);  // solid region

    cv::Mat opened = processor.open(mask, 1);
    EXPECT_EQ(opened.at<uchar>(5, 5), 0);
    EXPECT_EQ(opened.at<uchar>(27, 27), 255);
}

TEST(MorphologicalProcessorTest, CloseFillsSmallHoles) {
    MorphologicalProcessor processor(defaultConfig());
    cv::Mat mask = cv::Mat::zeros(50, 50, CV_8UC1);
    mask(cv::Rect(10, 10, 30, 30)).setTo(255);
    mask.at<uchar>(25, 25) = 0;  // small hole

    cv::Mat closed = processor.close(mask, 1);
    EXPECT_EQ(closed.at<uchar>(25, 25), 255);
}

TEST(MorphologicalProcessorTest, CleanCombinesOpenAndClose) {
    MorphologicalProcessor processor(defaultConfig());
    cv::Mat mask = cv::Mat::zeros(50, 50, CV_8UC1);
    mask.at<uchar>(2, 2) = 255;  // noise
    mask(cv::Rect(20, 20, 15, 15)).setTo(255);

    cv::Mat cleaned = processor.clean(mask);
    EXPECT_EQ(cleaned.at<uchar>(2, 2), 0);
    EXPECT_GT(cv::countNonZero(cleaned), 0);
}
