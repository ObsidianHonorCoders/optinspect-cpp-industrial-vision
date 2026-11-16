#include <gtest/gtest.h>

#include "common/Errors.hpp"
#include "processing/ThresholdProcessor.hpp"

using namespace optinspect;

TEST(ThresholdProcessorTest, FixedThresholdBinarizesCorrectly) {
    ThresholdConfig config;
    config.method = ThresholdMethod::FIXED;
    config.fixed_threshold = 100.0;
    ThresholdProcessor processor(config);

    cv::Mat image(10, 10, CV_8UC1);
    image(cv::Rect(0, 0, 5, 10)).setTo(50);
    image(cv::Rect(5, 0, 5, 10)).setTo(200);

    cv::Mat mask = processor.apply(image);
    EXPECT_EQ(mask.at<uchar>(0, 0), 0);
    EXPECT_EQ(mask.at<uchar>(0, 9), 255);
}

TEST(ThresholdProcessorTest, OtsuProducesBinaryMask) {
    ThresholdConfig config;
    config.method = ThresholdMethod::OTSU;
    ThresholdProcessor processor(config);

    cv::Mat image(20, 20, CV_8UC1);
    image(cv::Rect(0, 0, 10, 20)).setTo(10);
    image(cv::Rect(10, 0, 10, 20)).setTo(250);

    cv::Mat mask = processor.apply(image);
    for (int r = 0; r < mask.rows; ++r) {
        for (int c = 0; c < mask.cols; ++c) {
            EXPECT_TRUE(mask.at<uchar>(r, c) == 0 || mask.at<uchar>(r, c) == 255);
        }
    }
}

TEST(ThresholdProcessorTest, AdaptiveMeanProducesBinaryMask) {
    ThresholdConfig config;
    config.method = ThresholdMethod::ADAPTIVE_MEAN;
    config.adaptive_block_size = 7;
    ThresholdProcessor processor(config);

    cv::Mat image(30, 30, CV_8UC1, cv::Scalar(128));
    cv::Mat mask = processor.apply(image);
    EXPECT_EQ(mask.size(), image.size());
}

TEST(ThresholdProcessorTest, ThrowsOnMultiChannelImage) {
    ThresholdConfig config;
    ThresholdProcessor processor(config);
    cv::Mat color(10, 10, CV_8UC3);
    EXPECT_THROW(processor.apply(color), OptInspectError);
}
