#include "visualization/ImageRenderer.hpp"

#include <glad/glad.h>

namespace optinspect::viz {

TextureFilter chooseFilter(double zoom, bool has_mipmaps, double nearest_zoom) {
    if (zoom >= nearest_zoom) return TextureFilter::Nearest;
    if (zoom < 1.0 && has_mipmaps) return TextureFilter::LinearMipmap;
    return TextureFilter::Linear;
}

ImageRenderer::ImageRenderer(const std::filesystem::path& shader_dir, const VisualizationConfig& config)
    : config_(config),
      shader_(Shader::fromFiles(shader_dir / "image.vert", shader_dir / "image.frag")) {
    // Unit quad as a triangle strip: (0,0) top-left, (1,1) bottom-right.
    static const float kQuad[8] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    shader_.use();
    shader_.setInt("uTex0", 0);
    shader_.setInt("uTex1", 1);
}

ImageRenderer::~ImageRenderer() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void ImageRenderer::beginViewport(const RectI& vp, const Color4& background) {
    if (vp.empty()) return;
    glEnable(GL_SCISSOR_TEST);
    glViewport(vp.x, vp.y, vp.w, vp.h);
    glScissor(vp.x, vp.y, vp.w, vp.h);
    glClearColor(background.r, background.g, background.b, background.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void ImageRenderer::endViewport() { glDisable(GL_SCISSOR_TEST); }

void ImageRenderer::setRect(const ViewportTransform& t) {
    const Vec2 origin = t.viewportOrigin();
    const Vec2 size = t.viewportSize();
    const RectD r = t.imageScreenRect();
    const double left = (r.x - origin.x) / size.x * 2.0 - 1.0;
    const double top = 1.0 - (r.y - origin.y) / size.y * 2.0;
    const double width = r.w / size.x * 2.0;
    const double height = -(r.h / size.y * 2.0);
    shader_.setVec4("uRect", static_cast<float>(left), static_cast<float>(top),
                    static_cast<float>(width), static_cast<float>(height));
}

void ImageRenderer::applyFilter(Texture2D& texture, const ViewportTransform& t) {
    const TextureFilter wanted = chooseFilter(t.zoom(), texture.hasMipmaps(), config_.nearest_filter_zoom);
    const TextureFilter before = texture.filter();
    texture.setFilter(wanted);
    if (texture.filter() != before) ++stats_.filter_changes;
}

void ImageRenderer::drawImage(Texture2D& texture, const ViewportTransform& transform) {
    if (!texture.valid() || !transform.hasImage() || transform.viewportSize().x <= 0.0 ||
        transform.viewportSize().y <= 0.0) {
        return;
    }
    applyFilter(texture, transform);
    shader_.use();
    shader_.setInt("uMode", 0);
    setRect(transform);
    texture.bind(0);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    ++stats_.draw_calls;
}

void ImageRenderer::drawBlend(Texture2D& a, Texture2D& b, const ViewportTransform& transform, float blend) {
    if (!a.valid() || !b.valid() || !transform.hasImage() || transform.viewportSize().x <= 0.0 ||
        transform.viewportSize().y <= 0.0) {
        return;
    }
    applyFilter(a, transform);
    applyFilter(b, transform);
    shader_.use();
    shader_.setInt("uMode", 1);
    shader_.setFloat("uBlend", blend);
    setRect(transform);
    a.bind(0);
    b.bind(1);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    ++stats_.draw_calls;
}

}  // namespace optinspect::viz
