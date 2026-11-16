#include <gtest/gtest.h>

#include "processing/ImageProcessor.hpp"

using namespace optinspect;

TEST(ImageProcessorTest, ToGrayscaleConvertsColorImage) {
    cv::Mat color(10, 10, CV_8UC3, cv::Scalar(10, 20, 30));
    cv::Mat gray = ImageProcessor::toGrayscale(color);
    EXPECT_EQ(gray.channels(), 1);
    EXPECT_EQ(gray.size(), color.size());
}

TEST(ImageProcessorTest, ToGrayscaleIsNoOpOnGrayscaleImage) {
    cv::Mat gray(10, 10, CV_8UC1, cv::Scalar(100));
    cv::Mat result = ImageProcessor::toGrayscale(gray);
    EXPECT_EQ(result.channels(), 1);
}

TEST(ImageProcessorTest, GaussianBlurProducesSameSizeOutput) {
    cv::Mat image(50, 50, CV_8UC1, cv::Scalar(128));
    cv::Mat blurred = ImageProcessor::gaussianBlur(image, 5, 0.0);
    EXPECT_EQ(blurred.size(), image.size());
}

TEST(ImageProcessorTest, GaussianBlurHandlesEvenKernelSizeByRoundingUp) {
    cv::Mat image(50, 50, CV_8UC1, cv::Scalar(128));
    EXPECT_NO_THROW(ImageProcessor::gaussianBlur(image, 4, 0.0));
}

TEST(ImageProcessorTest, AbsoluteDifferenceOfIdenticalImagesIsZero) {
    cv::Mat a(20, 20, CV_8UC1, cv::Scalar(50));
    cv::Mat b = a.clone();
    cv::Mat diff = ImageProcessor::absoluteDifference(a, b);
    EXPECT_EQ(cv::countNonZero(diff), 0);
}

TEST(ImageProcessorTest, AbsoluteDifferenceDetectsChange) {
    cv::Mat a(20, 20, CV_8UC1, cv::Scalar(50));
    cv::Mat b(20, 20, CV_8UC1, cv::Scalar(200));
    cv::Mat diff = ImageProcessor::absoluteDifference(a, b);
    EXPECT_EQ(cv::countNonZero(diff), 20 * 20);
}

TEST(ImageProcessorTest, CropToRoiClampsToImageBounds) {
    cv::Mat image(50, 50, CV_8UC1, cv::Scalar(1));
    cv::Mat cropped = ImageProcessor::cropToRoi(image, cv::Rect(40, 40, 30, 30));
    EXPECT_LE(cropped.cols, 10);
    EXPECT_LE(cropped.rows, 10);
}
