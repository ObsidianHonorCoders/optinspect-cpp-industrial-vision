#pragma once

#include <opencv2/core.hpp>
#include <string>

namespace optinspect {

/// Abstract source of images for inspection. This separates "how an image
/// is obtained" from "how it is processed", so Phase 1's file-based source
/// can later be replaced or supplemented by a live camera source (Phase 3+)
/// without touching the processing or detection modules.
class ImageSource {
public:
    virtual ~ImageSource() = default;

    /// Loads and returns the image in BGR (OpenCV default) color order.
    /// Throws ImageLoadError if the image cannot be found, read, or decoded,
    /// or if it fails basic validity checks (non-empty, positive dimensions).
    virtual cv::Mat load() const = 0;

    /// Human-readable identifier for the source (e.g. the file path), used
    /// in log messages and inspection reports.
    virtual std::string describe() const = 0;
};

}  // namespace optinspect
