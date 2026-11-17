#pragma once

#include "visualization/VisualizationConfig.hpp"
#include "visualization/VisualizationTypes.hpp"

namespace optinspect::viz {

/// Pure coordinate-transformation model for one image viewport. It contains
/// no OpenGL or UI code, so it is fully unit-testable.
///
/// Conventions
///  - Image space: pixels of the source image, origin at the top-left corner
///    of pixel (0,0), x to the right, y down (same as cv::Mat).
///  - Screen space: the coordinate system the UI reports the cursor in
///    (ImGui/window logical coordinates, origin top-left, y down).
///  - The viewport is the screen rectangle (origin, size) the image is shown in.
///
/// Mapping:  screen = viewport_origin + offset + image * zoom
/// A single uniform `zoom` is used for both axes, so the image aspect ratio
/// is preserved by construction.
class ViewportTransform {
public:
    explicit ViewportTransform(const VisualizationConfig& config = {});

    /// Sets the size of the image being shown. A different size resets the
    /// view to "fit"; the same size keeps the current zoom and pan.
    void setImageSize(int width, int height);
    void setViewport(Vec2 origin, Vec2 size);

    int imageWidth() const { return image_w_; }
    int imageHeight() const { return image_h_; }
    Vec2 viewportOrigin() const { return origin_; }
    Vec2 viewportSize() const { return size_; }
    bool hasImage() const { return image_w_ > 0 && image_h_ > 0; }

    /// Zoom that makes the whole image visible (minus fit margin); never above max_zoom.
    double fitZoom() const;

    /// Fits the image to the viewport, centred. Keeps following the viewport
    /// size (re-fits on resize) until the user zooms or pans.
    void fit();
    /// 1:1 pixel view centred in the viewport.
    void actualSize();
    /// Resets zoom and pan; identical to fit().
    void reset() { fit(); }

    /// Multiplies the zoom by `factor`, keeping the image point under
    /// `screen_point` fixed. The result is clamped to the zoom limits.
    void zoomAt(Vec2 screen_point, double factor);
    void setZoom(double zoom);  ///< Zoom around the viewport centre.
    /// Moves the image by `screen_delta` pixels, then clamps so a part of the
    /// image always stays inside the viewport.
    void panBy(Vec2 screen_delta);
    /// Scrolls so that `image_point` is at the viewport centre.
    void centerOn(Vec2 image_point);

    double zoom() const { return zoom_; }
    Vec2 offset() const { return offset_; }
    bool followsFit() const { return follow_fit_; }
    double minZoom() const;
    double maxZoom() const { return config_.max_zoom; }

    /// Copies zoom, offset and fit-following from another transform (used to
    /// synchronize the two viewports of the side-by-side mode). Image and
    /// viewport sizes are not copied.
    void copyViewFrom(const ViewportTransform& other);

    Vec2 imageToScreen(Vec2 image_point) const;
    Vec2 screenToImage(Vec2 screen_point) const;
    RectD imageRectToScreen(const RectD& image_rect) const;
    RectD screenRectToImage(const RectD& screen_rect) const;

    /// Screen rectangle currently covered by the whole image.
    RectD imageScreenRect() const;

    /// True when `screen_point` is inside the viewport rectangle.
    bool viewportContains(Vec2 screen_point) const;
    /// True when `screen_point` is inside the displayed image.
    bool imageContains(Vec2 screen_point) const;

    /// Converts a drag between two screen points into an integer pixel
    /// rectangle in image space: clamped to the image, expanded outward to
    /// whole pixels (floor of the minimum, ceil of the maximum). Returns an
    /// empty rectangle when the drag does not intersect the image.
    RectI imageRectFromScreenDrag(Vec2 screen_a, Vec2 screen_b) const;

    /// Clamps a rectangle to the image bounds and rounds outward to whole pixels.
    RectI clampToImage(const RectD& image_rect) const;

private:
    void clampPan();
    void applyFitIfFollowing();

    VisualizationConfig config_;
    int image_w_ = 0;
    int image_h_ = 0;
    Vec2 origin_{0.0, 0.0};
    Vec2 size_{0.0, 0.0};
    double zoom_ = 1.0;
    Vec2 offset_{0.0, 0.0};  ///< Image origin relative to the viewport origin, in screen pixels.
    bool follow_fit_ = true;
};

/// Converts a screen rectangle (logical coordinates, top-left origin) to the
/// rectangle glViewport/glScissor expect: framebuffer pixels with the origin
/// at the BOTTOM-left of the framebuffer. `scale_x/scale_y` is the
/// framebuffer-to-screen ratio (greater than 1 on high-DPI displays).
RectI toGlViewportRect(const RectD& screen_rect, double scale_x, double scale_y,
                       int framebuffer_height);

}  // namespace optinspect::viz
