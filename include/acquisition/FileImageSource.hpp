#pragma once

#include "acquisition/ImageSource.hpp"

namespace optinspect {

/// Loads images from the local filesystem using OpenCV's image codecs.
/// Validates path existence/readability and basic image integrity
/// (non-empty, positive width/height, decodable format) before returning.
class FileImageSource final : public ImageSource {
public:
    explicit FileImageSource(std::string path);

    cv::Mat load() const override;
    std::string describe() const override;

private:
    std::string path_;
};

}  // namespace optinspect
