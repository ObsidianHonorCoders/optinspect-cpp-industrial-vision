#pragma once

#include "visualization/ViewportTransform.hpp"
#include "visualization/VisualizationConfig.hpp"
#include "visualization/VisualizationTypes.hpp"

#include <optional>

namespace optinspect::viz {

enum class ViewportTool {
    Pan,  ///< Left-drag pans the image.
    Roi,  ///< Left-drag selects a rectangular region of interest.
};

/// Pointer state of one frame in screen coordinates. The UI layer fills it from
/// the real input (ImGui in the GUI, synthetic values in tests), which keeps
/// ImageViewport free of any UI or OpenGL dependency.
struct PointerState {
    Vec2 cursor;
    bool hovered = false;        ///< Cursor is over the viewport and the viewport may take input.
    double wheel = 0.0;          ///< Wheel notches this frame (positive = away from the user = zoom in).
    bool left_pressed = false;   ///< Left button went down this frame.
    bool left_down = false;
    bool left_released = false;  ///< Left button went up this frame.
    bool middle_down = false;
    Vec2 delta;                  ///< Cursor movement since the previous frame.
};

/// Interactive state of one image viewport: zoom/pan (via ViewportTransform),
/// cursor-to-image mapping, click detection and ROI selection. Contains no
/// rendering code; ImageRenderer/DefectOverlayRenderer draw from its transform.
///
/// The ROI is stored in IMAGE pixels, so it stays attached to the image while
/// zooming and panning. It is a visualization/analysis aid only: it never
/// feeds back into the inspection.
class ImageViewport {
public:
    explicit ImageViewport(const VisualizationConfig& config = {});

    ViewportTransform& transform() { return transform_; }
    const ViewportTransform& transform() const { return transform_; }

    void setTool(ViewportTool tool) { tool_ = tool; }
    ViewportTool tool() const { return tool_; }

    /// Applies one frame of input (wheel zoom around the cursor, drag pan, ROI drag, click detection).
    void update(const PointerState& pointer);

    /// Image position under the cursor, or nullopt when the cursor is outside the image.
    std::optional<Vec2> cursorImagePosition() const { return cursor_image_; }
    /// Screen position of the last update (for display next to the cursor).
    Vec2 cursorScreenPosition() const { return cursor_screen_; }

    /// A left click (press+release with almost no movement) in the Pan tool.
    /// Returns its image position once, then clears it.
    std::optional<Vec2> takeClick();

    // ROI -------------------------------------------------------------
    /// Committed ROI in image pixels, if any.
    std::optional<RectI> roi() const { return roi_; }
    /// ROI while dragging (live preview), else the committed ROI.
    std::optional<RectI> roiForDisplay() const;
    bool roiDragActive() const { return roi_dragging_; }
    void setRoi(const RectI& roi);
    void clearRoi();

private:
    ViewportTransform transform_;
    VisualizationConfig config_;
    ViewportTool tool_ = ViewportTool::Pan;

    std::optional<Vec2> cursor_image_;
    Vec2 cursor_screen_;
    std::optional<Vec2> click_;

    bool dragging_ = false;      ///< A left press started inside the viewport.
    double drag_distance_ = 0.0; ///< Accumulated cursor movement of that press.
    Vec2 press_screen_;

    bool roi_dragging_ = false;
    Vec2 roi_start_image_;
    std::optional<RectI> roi_preview_;
    std::optional<RectI> roi_;
};

}  // namespace optinspect::viz
