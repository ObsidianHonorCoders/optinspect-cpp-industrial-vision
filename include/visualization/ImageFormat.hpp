#pragma once

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>

namespace optinspect::viz {

/// Pixel layouts the texture path supports. They map 1:1 to 8-bit OpenCV
/// matrices, so uploads never need a colour conversion on the CPU:
/// the BGR/BGRA channel order is handled by the GL upload format (GL_BGR/GL_BGRA).
enum class PixelLayout { Gray8, BGR8, BGRA8 };

const char* toString(PixelLayout layout);

enum class FormatStatus {
    Ok,
    Empty,
    UnsupportedDepth,
    UnsupportedChannels,
    InvalidDimensions,
    TooLarge,
};

struct ImageFormat {
    PixelLayout layout = PixelLayout::BGR8;
    int width = 0;
    int height = 0;
    int channels = 0;

    std::size_t bytesPerPixel() const { return static_cast<std::size_t>(channels); }
    /// Size of the uncompressed texture in GPU memory (level 0 only).
    std::size_t gpuBytes() const {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * bytesPerPixel();
    }
};

struct FormatCheck {
    FormatStatus status = FormatStatus::Empty;
    ImageFormat format;
    std::string message;  ///< Human-readable reason when status != Ok.

    bool ok() const { return status == FormatStatus::Ok; }
};

/// Validates a cv::Mat for texture upload: 8-bit, 1/3/4 channels, 2-D,
/// non-empty and not larger than `max_dimension` per side.
FormatCheck inspectImage(const cv::Mat& image, int max_dimension = 16384);

/// How the pixels of a cv::Mat are handed to glTexImage2D/glTexSubImage2D.
struct UploadPlan {
    cv::Mat source;           ///< Matrix whose memory is uploaded (the input itself or a contiguous copy).
    int row_length_px = 0;    ///< GL_UNPACK_ROW_LENGTH; 0 = rows are tightly packed.
    bool copied = false;      ///< True when a contiguous copy had to be made.

    const unsigned char* data() const { return source.data; }
};

/// Chooses the cheapest correct way to upload `image` (assumed valid, see
/// inspectImage):
///  - continuous matrix:           upload in place, tightly packed;
///  - sub-matrix / padded rows whose stride is a multiple of the pixel size:
///                                 upload in place using GL_UNPACK_ROW_LENGTH;
///  - any other stride:            deliberate contiguous copy.
UploadPlan planUpload(const cv::Mat& image);

}  // namespace optinspect::viz
