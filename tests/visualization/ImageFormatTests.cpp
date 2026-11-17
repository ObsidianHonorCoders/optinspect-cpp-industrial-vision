#include "visualization/ImageFormat.hpp"

#include <gtest/gtest.h>

using namespace optinspect::viz;

TEST(ImageFormat, GrayscaleIsGray8) {
    const cv::Mat m(10, 20, CV_8UC1, cv::Scalar(7));
    const FormatCheck c = inspectImage(m);
    ASSERT_TRUE(c.ok());
    EXPECT_EQ(c.format.layout, PixelLayout::Gray8);
    EXPECT_EQ(c.format.channels, 1);
    EXPECT_EQ(c.format.width, 20);
    EXPECT_EQ(c.format.height, 10);
    EXPECT_EQ(c.format.gpuBytes(), 200u);
}

TEST(ImageFormat, BgrIsBgr8) {
    const cv::Mat m(8, 16, CV_8UC3, cv::Scalar(1, 2, 3));
    const FormatCheck c = inspectImage(m);
    ASSERT_TRUE(c.ok());
    EXPECT_EQ(c.format.layout, PixelLayout::BGR8);
    EXPECT_EQ(c.format.bytesPerPixel(), 3u);
    EXPECT_EQ(c.format.gpuBytes(), 8u * 16u * 3u);
}

TEST(ImageFormat, BgraIsBgra8) {
    const cv::Mat m(4, 4, CV_8UC4, cv::Scalar(1, 2, 3, 255));
    const FormatCheck c = inspectImage(m);
    ASSERT_TRUE(c.ok());
    EXPECT_EQ(c.format.layout, PixelLayout::BGRA8);
    EXPECT_EQ(c.format.gpuBytes(), 4u * 4u * 4u);
}

TEST(ImageFormat, EmptyImageIsRejected) {
    EXPECT_EQ(inspectImage(cv::Mat()).status, FormatStatus::Empty);
    EXPECT_FALSE(inspectImage(cv::Mat()).message.empty());
    EXPECT_EQ(inspectImage(cv::Mat(0, 10, CV_8UC3)).status, FormatStatus::Empty);
}

TEST(ImageFormat, UnsupportedDepthsAreRejected) {
    EXPECT_EQ(inspectImage(cv::Mat(4, 4, CV_16UC1)).status, FormatStatus::UnsupportedDepth);
    EXPECT_EQ(inspectImage(cv::Mat(4, 4, CV_32FC3)).status, FormatStatus::UnsupportedDepth);
    EXPECT_EQ(inspectImage(cv::Mat(4, 4, CV_8SC1)).status, FormatStatus::UnsupportedDepth);
}

TEST(ImageFormat, UnsupportedChannelCountsAreRejected) {
    EXPECT_EQ(inspectImage(cv::Mat(4, 4, CV_8UC2)).status, FormatStatus::UnsupportedChannels);
    EXPECT_EQ(inspectImage(cv::Mat(4, 4, CV_8UC(5))).status, FormatStatus::UnsupportedChannels);
}

TEST(ImageFormat, ThreeDimensionalMatricesAreRejected) {
    const int sizes[3] = {2, 3, 4};
    const cv::Mat m(3, sizes, CV_8UC1, cv::Scalar(0));
    EXPECT_EQ(inspectImage(m).status, FormatStatus::InvalidDimensions);
}

TEST(ImageFormat, OversizedImagesAreRejected) {
    const cv::Mat m(10, 5000, CV_8UC1);
    EXPECT_TRUE(inspectImage(m, 8192).ok());
    EXPECT_EQ(inspectImage(m, 4096).status, FormatStatus::TooLarge);
}

TEST(ImageFormat, DimensionsAreReported) {
    const FormatCheck c = inspectImage(cv::Mat(480, 640, CV_8UC3));
    EXPECT_EQ(c.format.width, 640);
    EXPECT_EQ(c.format.height, 480);
}

// --- upload planning -----------------------------------------------------------

TEST(UploadPlan, ContinuousMatrixIsUploadedInPlace) {
    const cv::Mat m(10, 10, CV_8UC3, cv::Scalar(1, 2, 3));
    const UploadPlan p = planUpload(m);
    EXPECT_FALSE(p.copied);
    EXPECT_EQ(p.row_length_px, 0);
    EXPECT_EQ(p.data(), m.data);
}

TEST(UploadPlan, SubMatrixUsesRowLengthWithoutCopy) {
    cv::Mat big(100, 200, CV_8UC3, cv::Scalar(9, 9, 9));
    const cv::Mat roi = big(cv::Rect(10, 20, 50, 30));
    ASSERT_FALSE(roi.isContinuous());
    const UploadPlan p = planUpload(roi);
    EXPECT_FALSE(p.copied);
    EXPECT_EQ(p.row_length_px, 200);  // step 600 bytes / 3 bytes per pixel
    EXPECT_EQ(p.data(), roi.data);
}

TEST(UploadPlan, GrayscaleSubMatrixRowLengthEqualsStride) {
    cv::Mat big(64, 128, CV_8UC1, cv::Scalar(5));
    const cv::Mat roi = big(cv::Rect(4, 4, 32, 16));
    const UploadPlan p = planUpload(roi);
    EXPECT_FALSE(p.copied);
    EXPECT_EQ(p.row_length_px, 128);
}

TEST(UploadPlan, StrideNotMultipleOfPixelSizeIsCopiedContiguously) {
    // 3-byte pixels with a 100-byte row stride (not a multiple of 3).
    std::vector<unsigned char> buffer(100 * 10, 0);
    for (int i = 0; i < 10; ++i) buffer[static_cast<size_t>(i) * 100] = static_cast<unsigned char>(i + 1);
    cv::Mat strided(10, 20, CV_8UC3, buffer.data(), 100);
    ASSERT_FALSE(strided.isContinuous());
    const UploadPlan p = planUpload(strided);
    EXPECT_TRUE(p.copied);
    EXPECT_EQ(p.row_length_px, 0);
    ASSERT_TRUE(p.source.isContinuous());
    EXPECT_NE(p.data(), strided.data);
    // Content is preserved row by row.
    for (int r = 0; r < 10; ++r) EXPECT_EQ(p.source.at<cv::Vec3b>(r, 0)[0], r + 1);
}

TEST(UploadPlan, OddWidthRowsAreNotPaddedByThePlan) {
    // Width 5, 1 channel: rows are 5 bytes - not a multiple of GL's default alignment of 4.
    // The plan reports tightly packed rows; the texture code sets GL_UNPACK_ALIGNMENT to 1.
    const cv::Mat m(7, 5, CV_8UC1, cv::Scalar(3));
    const UploadPlan p = planUpload(m);
    EXPECT_EQ(p.row_length_px, 0);
    EXPECT_EQ(m.step[0], 5u);
}
