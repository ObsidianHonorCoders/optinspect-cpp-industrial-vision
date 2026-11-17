#pragma once

#include "visualization/ImageFormat.hpp"

#include <opencv2/core.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>

namespace optinspect::viz {

class TextureError : public std::runtime_error {
public:
    explicit TextureError(const std::string& message) : std::runtime_error(message) {}
};

enum class TextureFilter { Nearest, Linear, LinearMipmap };

/// RAII wrapper around one OpenGL 2D texture holding an OpenCV image.
///
/// Orientation: OpenCV row 0 is the TOP row. The pixels are uploaded in that
/// order, so texture coordinate t = 0 is the image top; ImageRenderer's quad
/// uses t = 0 at the top edge, so no vertical flip is ever needed.
/// Colour: BGR/BGRA data is uploaded as GL_BGR/GL_BGRA (the driver reorders),
/// grey images use GL_R8 with a (R,R,R,1) swizzle - no CPU conversion either way.
/// Rows: GL_UNPACK_ALIGNMENT is 1 and GL_UNPACK_ROW_LENGTH is set for padded or
/// sub-matrix inputs (see planUpload), so cv::Mat strides are honoured.
///
/// upload() only re-allocates storage (glTexImage2D) when width, height or
/// layout change; otherwise it overwrites the contents (glTexSubImage2D).
/// Requires the owning GL context to be current for all calls, including the destructor.
class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D();
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    /// Uploads (or replaces) the image. Throws TextureError for an invalid
    /// image (empty, unsupported depth/channels, too large) or when the GPU
    /// allocation fails; the previous contents are kept when validation fails.
    void upload(const cv::Mat& image, bool generate_mipmaps = true);

    void bind(unsigned int unit = 0) const;
    void setFilter(TextureFilter filter);
    void release();  ///< Frees the GPU storage; the object can be reused.

    bool valid() const { return id_ != 0; }
    unsigned int id() const { return id_; }
    int width() const { return format_.width; }
    int height() const { return format_.height; }
    const ImageFormat& format() const { return format_; }
    bool hasMipmaps() const { return mipmaps_; }
    TextureFilter filter() const { return filter_; }

    /// Approximate GPU memory held by the texture (including the mip chain, +1/3).
    std::size_t gpuBytes() const;

    /// Number of glTexImage2D allocations / glTexSubImage2D replacements so far.
    std::size_t allocationCount() const { return allocations_; }
    std::size_t updateCount() const { return updates_; }

private:
    unsigned int id_ = 0;
    ImageFormat format_;
    bool mipmaps_ = false;
    TextureFilter filter_ = TextureFilter::Linear;
    std::size_t allocations_ = 0;
    std::size_t updates_ = 0;
};

}  // namespace optinspect::viz
