#pragma once

#include "visualization/Shader.hpp"
#include "visualization/Texture2D.hpp"
#include "visualization/ViewportTransform.hpp"
#include "visualization/VisualizationConfig.hpp"

#include <filesystem>

namespace optinspect::viz {

/// Filter to use for an image shown at `zoom`: pixel-exact (nearest) when
/// magnified beyond `nearest_zoom`, trilinear mipmapped when minified (if the
/// texture has a mip chain), plain bilinear otherwise.
TextureFilter chooseFilter(double zoom, bool has_mipmaps, double nearest_zoom);

struct RenderStats {
    std::size_t draw_calls = 0;      ///< Image draw calls since construction.
    std::size_t filter_changes = 0;  ///< Texture filter state changes issued by the renderer.
};

/// Draws textured 2D images into a sub-rectangle (viewport) of the current framebuffer.
///
/// Pipeline (per viewport):
///   1. beginViewport(): glViewport + scissor to the panel rectangle, clear it.
///   2. drawImage()/drawBlend(): the image is ONE quad (unit square, VAO + VBO, no EBO:
///      a 4-vertex triangle strip needs no index buffer). The vertex shader places the
///      quad using the ViewportTransform (zoom/pan/fit) - no geometry is rebuilt on
///      zoom or pan, only one vec4 uniform changes.
///   3. endViewport(): scissor off.
/// The aspect ratio is preserved because ViewportTransform uses a single uniform zoom.
/// The shader program, VAO and VBO are created once and reused for every frame and
/// every image, so replacing an image never reallocates renderer resources.
///
/// Requires a current OpenGL context for construction, use and destruction.
class ImageRenderer {
public:
    ImageRenderer(const std::filesystem::path& shader_dir, const VisualizationConfig& config);
    ~ImageRenderer();
    ImageRenderer(const ImageRenderer&) = delete;
    ImageRenderer& operator=(const ImageRenderer&) = delete;

    void beginViewport(const RectI& gl_viewport, const Color4& background);
    void endViewport();

    void drawImage(Texture2D& texture, const ViewportTransform& transform);
    /// mix(a, b, blend). Both textures are sampled with the normalized
    /// coordinates of `a`, so they should have identical dimensions.
    void drawBlend(Texture2D& a, Texture2D& b, const ViewportTransform& transform, float blend);

    const RenderStats& stats() const { return stats_; }

private:
    void setRect(const ViewportTransform& transform);
    void applyFilter(Texture2D& texture, const ViewportTransform& transform);

    VisualizationConfig config_;
    Shader shader_;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    RenderStats stats_;
};

}  // namespace optinspect::viz
