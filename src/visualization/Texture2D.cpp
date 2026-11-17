#include "visualization/Texture2D.hpp"

#include <glad/glad.h>

#include <utility>

namespace optinspect::viz {

namespace {

struct GlFormat {
    GLint internal_format;
    GLenum format;
};

GlFormat glFormatFor(PixelLayout layout) {
    switch (layout) {
        case PixelLayout::Gray8: return {GL_R8, GL_RED};
        case PixelLayout::BGR8: return {GL_RGB8, GL_BGR};
        case PixelLayout::BGRA8: return {GL_RGBA8, GL_BGRA};
    }
    return {GL_RGB8, GL_BGR};
}

GLenum glFilterMin(TextureFilter f) {
    switch (f) {
        case TextureFilter::Nearest: return GL_NEAREST;
        case TextureFilter::Linear: return GL_LINEAR;
        case TextureFilter::LinearMipmap: return GL_LINEAR_MIPMAP_LINEAR;
    }
    return GL_LINEAR;
}

GLenum glFilterMag(TextureFilter f) { return f == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR; }

void drainGlErrors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

}  // namespace

Texture2D::~Texture2D() { release(); }

Texture2D::Texture2D(Texture2D&& other) noexcept
    : id_(std::exchange(other.id_, 0)),
      format_(other.format_),
      mipmaps_(other.mipmaps_),
      filter_(other.filter_),
      allocations_(other.allocations_),
      updates_(other.updates_) {}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        release();
        id_ = std::exchange(other.id_, 0);
        format_ = other.format_;
        mipmaps_ = other.mipmaps_;
        filter_ = other.filter_;
        allocations_ = other.allocations_;
        updates_ = other.updates_;
    }
    return *this;
}

void Texture2D::release() {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
    format_ = {};
    mipmaps_ = false;
}

void Texture2D::upload(const cv::Mat& image, bool generate_mipmaps) {
    GLint max_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    const FormatCheck check = inspectImage(image, max_size > 0 ? max_size : 16384);
    if (!check.ok()) throw TextureError("Cannot create texture: " + check.message);

    const UploadPlan plan = planUpload(image);
    const GlFormat gl = glFormatFor(check.format.layout);
    const bool same_storage = id_ != 0 && format_.width == check.format.width &&
                              format_.height == check.format.height &&
                              format_.layout == check.format.layout;

    drainGlErrors();
    if (id_ == 0) {
        glGenTextures(1, &id_);
        if (id_ == 0) throw TextureError("glGenTextures failed");
    }
    glBindTexture(GL_TEXTURE_2D, id_);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, plan.row_length_px);

    if (same_storage) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, check.format.width, check.format.height, gl.format,
                        GL_UNSIGNED_BYTE, plan.data());
        ++updates_;
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, gl.internal_format, check.format.width, check.format.height, 0,
                     gl.format, GL_UNSIGNED_BYTE, plan.data());
        ++allocations_;
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (!same_storage) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (check.format.layout == PixelLayout::Gray8) {
            const GLint swizzle[4] = {GL_RED, GL_RED, GL_RED, GL_ONE};
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
        } else {
            const GLint identity[4] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, identity);
        }
    }

    mipmaps_ = generate_mipmaps;
    if (generate_mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    format_ = check.format;

    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        const bool oom = error == GL_OUT_OF_MEMORY;
        release();
        throw TextureError(std::string("OpenGL error while uploading texture (") +
                           (oom ? "out of GPU memory" : "GL error " + std::to_string(error)) + ")");
    }

    // Re-apply the filter: the mip chain may have appeared or disappeared.
    if (filter_ == TextureFilter::LinearMipmap && !mipmaps_) filter_ = TextureFilter::Linear;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(glFilterMin(filter_)));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(glFilterMag(filter_)));
}

void Texture2D::bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, id_);
}

void Texture2D::setFilter(TextureFilter filter) {
    if (id_ == 0) return;
    if (filter == TextureFilter::LinearMipmap && !mipmaps_) filter = TextureFilter::Linear;
    if (filter == filter_) return;  // avoid redundant GL state changes
    filter_ = filter;
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(glFilterMin(filter_)));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(glFilterMag(filter_)));
}

std::size_t Texture2D::gpuBytes() const {
    const std::size_t base = format_.gpuBytes();
    return mipmaps_ ? base + base / 3 : base;
}

}  // namespace optinspect::viz
