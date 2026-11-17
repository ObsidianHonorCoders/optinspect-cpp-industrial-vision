#include "visualization/DefectOverlayRenderer.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>

namespace optinspect::viz {

namespace {

void pushVertex(std::vector<float>& v, double x, double y, const Color4& c) {
    v.push_back(static_cast<float>(x));
    v.push_back(static_cast<float>(y));
    v.push_back(c.r);
    v.push_back(c.g);
    v.push_back(c.b);
    v.push_back(c.a);
}

void pushQuad(std::vector<float>& v, double x0, double y0, double x1, double y1, const Color4& c) {
    pushVertex(v, x0, y0, c);
    pushVertex(v, x1, y0, c);
    pushVertex(v, x0, y1, c);
    pushVertex(v, x1, y0, c);
    pushVertex(v, x1, y1, c);
    pushVertex(v, x0, y1, c);
}

}  // namespace

bool appendRect(OverlayGeometry& out, const ViewportTransform& transform, const RectD& image_rect,
                const Color4& outline, float thickness_px, const Color4* fill, float min_box_px) {
    if (image_rect.w < 0.0 || image_rect.h < 0.0) return false;

    const Vec2 origin = transform.viewportOrigin();
    const Vec2 size = transform.viewportSize();
    RectD s = transform.imageRectToScreen(image_rect);

    // Inflate tiny boxes symmetrically so they remain visible at low zoom.
    if (s.w < min_box_px) {
        s.x -= (min_box_px - s.w) * 0.5;
        s.w = min_box_px;
    }
    if (s.h < min_box_px) {
        s.y -= (min_box_px - s.h) * 0.5;
        s.h = min_box_px;
    }

    // Cull shapes that lie completely outside the viewport (outline included).
    const double half = thickness_px * 0.5;
    if (s.right() + half < origin.x || s.x - half > origin.x + size.x ||
        s.bottom() + half < origin.y || s.y - half > origin.y + size.y) {
        return false;
    }

    // Viewport-local coordinates.
    const double x0 = s.x - origin.x;
    const double y0 = s.y - origin.y;
    const double x1 = x0 + s.w;
    const double y1 = y0 + s.h;

    if (fill && fill->a > 0.0f) pushQuad(out.vertices, x0, y0, x1, y1, *fill);

    // Four bars centred on the box edges (corners overlap, which is harmless for opaque outlines).
    const double t = half;
    pushQuad(out.vertices, x0 - t, y0 - t, x1 + t, y0 + t, outline);  // top
    pushQuad(out.vertices, x0 - t, y1 - t, x1 + t, y1 + t, outline);  // bottom
    pushQuad(out.vertices, x0 - t, y0 + t, x0 + t, y1 - t, outline);  // left
    pushQuad(out.vertices, x1 - t, y0 + t, x1 + t, y1 - t, outline);  // right
    ++out.shape_count;
    return true;
}

OverlayGeometry buildDefectOverlay(const std::vector<DefectBox>& defects,
                                   std::optional<int> selected_index,
                                   const ViewportTransform& transform, const OverlayStyle& style) {
    OverlayGeometry out;
    out.vertices.reserve(defects.size() * 24 * OverlayGeometry::kFloatsPerVertex);

    const DefectBox* selected = nullptr;
    for (const DefectBox& d : defects) {
        if (selected_index && d.index == *selected_index) {
            selected = &d;
            continue;
        }
        if (appendRect(out, transform, d.image_rect, style.defect_outline, style.defect_thickness_px,
                       nullptr, style.min_box_px)) {
            const Vec2 anchor = transform.imageToScreen({d.image_rect.x, d.image_rect.y});
            out.labels.push_back({anchor, d.index, d.area_px, false});
        }
    }
    if (selected) {
        if (appendRect(out, transform, selected->image_rect, style.selected_outline,
                       style.selected_thickness_px, &style.selected_fill, style.min_box_px)) {
            const Vec2 anchor = transform.imageToScreen({selected->image_rect.x, selected->image_rect.y});
            out.labels.push_back({anchor, selected->index, selected->area_px, true});
        }
    }
    return out;
}

void appendRoi(OverlayGeometry& out, const ViewportTransform& transform, const RectD& roi_image_rect,
               const OverlayStyle& style) {
    appendRect(out, transform, roi_image_rect, style.roi_outline, style.roi_thickness_px, &style.roi_fill,
               0.0f);
}

std::optional<int> hitTestDefects(const std::vector<DefectBox>& defects, Vec2 p, double tolerance_px) {
    std::optional<int> best;
    double best_area = 0.0;
    for (const DefectBox& d : defects) {
        const RectD& r = d.image_rect;
        if (p.x >= r.x - tolerance_px && p.x <= r.right() + tolerance_px && p.y >= r.y - tolerance_px &&
            p.y <= r.bottom() + tolerance_px) {
            const double a = r.area();
            if (!best || a < best_area) {
                best = d.index;
                best_area = a;
            }
        }
    }
    return best;
}

DefectOverlayRenderer::DefectOverlayRenderer(const std::filesystem::path& shader_dir)
    : shader_(Shader::fromFiles(shader_dir / "overlay.vert", shader_dir / "overlay.frag")) {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = OverlayGeometry::kFloatsPerVertex * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
}

DefectOverlayRenderer::~DefectOverlayRenderer() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void DefectOverlayRenderer::render(const OverlayGeometry& geometry, Vec2 viewport_size) {
    if (geometry.vertices.empty() || viewport_size.x <= 0.0 || viewport_size.y <= 0.0) return;

    const std::size_t bytes = geometry.vertices.size() * sizeof(float);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    if (bytes > capacity_bytes_) {
        capacity_bytes_ = std::max(bytes, capacity_bytes_ * 2);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity_bytes_), nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), geometry.vertices.data());

    shader_.use();
    shader_.setVec2("uViewportSize", static_cast<float>(viewport_size.x), static_cast<float>(viewport_size.y));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(geometry.vertexCount()));
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    ++draw_calls_;
}

}  // namespace optinspect::viz
