#pragma once

#include <cstddef>

namespace optinspect::viz {

/// Plain 2D point/vector. Used for both image-space (pixels of the source
/// image, origin at the top-left, y pointing down) and screen-space values;
/// the name of the variable or function says which one.
struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }

/// Axis-aligned rectangle with a floating-point origin and size.
struct RectD {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    double right() const { return x + w; }
    double bottom() const { return y + h; }
    double area() const { return w * h; }
    bool empty() const { return w <= 0.0 || h <= 0.0; }
    bool contains(Vec2 p) const { return p.x >= x && p.x < right() && p.y >= y && p.y < bottom(); }
};

/// Integer pixel rectangle (image pixels or framebuffer pixels).
struct RectI {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool empty() const { return w <= 0 || h <= 0; }
    long long area() const { return static_cast<long long>(w) * h; }
    bool operator==(const RectI& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const RectI& o) const { return !(*this == o); }
};

struct Color4 {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

/// What the central viewport shows.
enum class ViewMode { Reference, Inspected, Annotated, Difference, SideBySide, Blend };

inline const char* toString(ViewMode mode) {
    switch (mode) {
        case ViewMode::Reference: return "Reference";
        case ViewMode::Inspected: return "Inspected";
        case ViewMode::Annotated: return "Annotated";
        case ViewMode::Difference: return "Difference";
        case ViewMode::SideBySide: return "Side-by-side";
        case ViewMode::Blend: return "Blend";
    }
    return "?";
}

/// Visual style of the defect overlay primitives. Thicknesses are in screen
/// pixels and stay constant regardless of zoom.
struct OverlayStyle {
    Color4 defect_outline{1.0f, 0.25f, 0.2f, 1.0f};
    Color4 selected_outline{1.0f, 0.85f, 0.1f, 1.0f};
    Color4 selected_fill{1.0f, 0.85f, 0.1f, 0.18f};
    Color4 roi_outline{0.2f, 0.8f, 1.0f, 1.0f};
    Color4 roi_fill{0.2f, 0.8f, 1.0f, 0.15f};
    float defect_thickness_px = 2.0f;
    float selected_thickness_px = 3.5f;
    float roi_thickness_px = 2.0f;
    float min_box_px = 4.0f;  ///< Boxes smaller than this on screen are drawn at this size.
};

}  // namespace optinspect::viz
