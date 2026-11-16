#include "acquisition/FileImageSource.hpp"

#include <opencv2/imgcodecs.hpp>

#include <filesystem>

#include "common/Errors.hpp"

namespace optinspect {

namespace fs = std::filesystem;

FileImageSource::FileImageSource(std::string path) : path_(std::move(path)) {}

cv::Mat FileImageSource::load() const {
    std::error_code ec;
    if (!fs::exists(path_, ec) || ec) {
        throw ImageLoadError("Image path does not exist: " + path_);
    }
    if (!fs::is_regular_file(path_, ec) || ec) {
        throw ImageLoadError("Image path is not a regular file: " + path_);
    }

    cv::Mat image = cv::imread(path_, cv::IMREAD_COLOR);
    if (image.empty()) {
        throw ImageLoadError("Failed to decode image (unsupported or corrupted file): " + path_);
    }
    if (image.cols <= 0 || image.rows <= 0) {
        throw ImageLoadError("Decoded image has invalid dimensions: " + path_);
    }

    return image;
}

std::string FileImageSource::describe() const { return path_; }

}  // namespace optinspect
