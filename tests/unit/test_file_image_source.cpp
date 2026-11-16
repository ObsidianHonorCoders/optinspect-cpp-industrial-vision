#include <gtest/gtest.h>

#include "acquisition/FileImageSource.hpp"
#include "common/Errors.hpp"

#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace optinspect;

namespace {

fs::path tempImagePath(const std::string& name) {
    return fs::temp_directory_path() / ("optinspect_test_" + name);
}

}  // namespace

TEST(FileImageSourceTest, LoadsValidImage) {
    const auto path = tempImagePath("valid.png");
    cv::Mat image(50, 50, CV_8UC3, cv::Scalar(10, 20, 30));
    cv::imwrite(path.string(), image);

    FileImageSource source(path.string());
    cv::Mat loaded = source.load();

    EXPECT_FALSE(loaded.empty());
    EXPECT_EQ(loaded.cols, 50);
    EXPECT_EQ(loaded.rows, 50);

    fs::remove(path);
}

TEST(FileImageSourceTest, ThrowsOnNonexistentPath) {
    FileImageSource source((fs::temp_directory_path() / "optinspect_does_not_exist.png").string());
    EXPECT_THROW(source.load(), ImageLoadError);
}

TEST(FileImageSourceTest, ThrowsOnCorruptedFile) {
    const auto path = tempImagePath("corrupted.png");
    std::ofstream file(path, std::ios::binary);
    file << "this is not a valid image file";
    file.close();

    FileImageSource source(path.string());
    EXPECT_THROW(source.load(), ImageLoadError);

    fs::remove(path);
}

TEST(FileImageSourceTest, DescribeReturnsOriginalPath) {
    FileImageSource source("some/path.png");
    EXPECT_EQ(source.describe(), "some/path.png");
}
