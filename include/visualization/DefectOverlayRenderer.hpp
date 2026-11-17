#pragma once

#include "visualization/Shader.hpp"
#include "visualization/ViewportTransform.hpp"
#include "visualization/VisualizationTypes.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace optinspect::viz {

/// One detected region in image pixels, exactly as measured by Phase 1
/// (bounding box in image coordinates). `index` is the label shown to the user.
struct DefectBox {
    RectD image_rect;
    int index = 0;
    double area_px = 0.0;
};

/// Text anchor produced by the geometry builder; the UI layer draws the text
/// (Dear ImGui) so no font rendering lives in the GL module.
struct OverlayLabel {
    Vec2 screen_pos;  ///< Absolute screen position of the label's top-left.
    int index = 0;
    double area_px = 0.0;
    bool selected = false;
};

/// CPU-side result of mapping overlay shapes through a ViewportTransform:
/// triangles (6 vertices per quad) ready for one batched draw call.
/// Vertex layout: x, y (viewport-local pixels), r, g, b, a  (6 floats).
struct OverlayGeometry {
    static constexpr int kFloatsPerVertex = 6;
    std::vector<float> vertices;
    std::vector<OverlayLabel> labels;
    std::size_t shape_count = 0;  ///< Shapes that were actually emitted (visible ones).

    std::size_t vertexCount() const { return vertices.size() / kFloatsPerVertex; }
    void clear() {
        vertices.clear();
        labels.clear();
        shape_count = 0;
    }
};

/// Appends an outlined (and optionally filled) rectangle given in image
/// coordinates. The outline has a constant screen thickness, centred on the box
/// edge; boxes smaller than `min_box_px` on screen are inflated to that size so
/// tiny defects stay visible. Shapes completely outside the viewport are
/// culled. Returns true when something was emitted.
bool appendRect(OverlayGeometry& out, const ViewportTransform& transform, const RectD& image_rect,
                const Color4& outline, float thickness_px, const Color4* fill = nullptr,
                float min_box_px = 0.0f);

/// Maps defects to overlay geometry: ordinary candidates use the defect style,
/// the one whose `index` equals `selected_index` (if any) uses the selected style
/// (thicker outline + translucent fill) and is drawn last, on top.
OverlayGeometry buildDefectOverlay(const std::vector<DefectBox>& defects,
                                   std::optional<int> selected_index,
                                   const ViewportTransform& transform, const OverlayStyle& style);

/// Appends the ROI rectangle (image coordinates) in the ROI style.
void appendRoi(OverlayGeometry& out, const ViewportTransform& transform, const RectD& roi_image_rect,
               const OverlayStyle& style);

/// Index (DefectBox::index) of the defect under `image_point`, or nullopt.
/// `tolerance_px` grows each box by that many image pixels so tiny boxes can be
/// hit; if several boxes match, the one with the smallest area wins.
std::optional<int> hitTestDefects(const std::vector<DefectBox>& defects, Vec2 image_point,
                                  double tolerance_px = 0.0);

/// Batched renderer for OverlayGeometry. The overlay is drawn as separate
/// primitives on top of the image texture; nothing is ever baked into a texture.
/// One VAO/VBO and one draw call serve all shapes of a frame; the VBO grows
/// geometrically and is reused, so there is no per-frame allocation in steady state.
/// Requires a current OpenGL context.
class DefectOverlayRenderer {
public:
    explicit DefectOverlayRenderer(const std::filesystem::path& shader_dir);
    ~DefectOverlayRenderer();
    DefectOverlayRenderer(const DefectOverlayRenderer&) = delete;
    DefectOverlayRenderer& operator=(const DefectOverlayRenderer&) = delete;

    /// Draws into the current glViewport/scissor (set by ImageRenderer::beginViewport).
    /// `viewport_size` is the viewport size in the same unit as the geometry.
    void render(const OverlayGeometry& geometry, Vec2 viewport_size);

    std::size_t drawCalls() const { return draw_calls_; }
    std::size_t bufferCapacityBytes() const { return capacity_bytes_; }

private:
    Shader shader_;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    std::size_t capacity_bytes_ = 0;
    std::size_t draw_calls_ = 0;
};

}  // namespace optinspect::viz
