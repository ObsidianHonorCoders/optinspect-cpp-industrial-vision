#include "visualization/ImageViewport.hpp"

#include <algorithm>
#include <cmath>

namespace optinspect::viz {

namespace {
constexpr double kClickMaxTravelPx = 4.0;   // a press moving less than this is a click
constexpr double kRoiMinDragPx = 3.0;       // smaller ROI drags are treated as a click
}  // namespace

ImageViewport::ImageViewport(const VisualizationConfig& config) : transform_(config), config_(config) {}

void ImageViewport::update(const PointerState& p) {
    cursor_screen_ = p.cursor;
    cursor_image_.reset();
    if (transform_.hasImage() && p.hovered && transform_.imageContains(p.cursor)) {
        cursor_image_ = transform_.screenToImage(p.cursor);
    }

    // Wheel zoom around the cursor.
    if (p.hovered && p.wheel != 0.0) {
        transform_.zoomAt(p.cursor, std::pow(config_.zoom_step, p.wheel));
    }

    // Middle button always pans.
    if (p.hovered && p.middle_down && (p.delta.x != 0.0 || p.delta.y != 0.0)) {
        transform_.panBy(p.delta);
    }

    // Left button: begin / continue / end.
    if (p.left_pressed && p.hovered) {
        dragging_ = true;
        drag_distance_ = 0.0;
        press_screen_ = p.cursor;
        if (tool_ == ViewportTool::Roi && transform_.hasImage()) {
            roi_dragging_ = true;
            roi_start_image_ = transform_.screenToImage(p.cursor);
            roi_preview_.reset();
        }
    }

    if (dragging_ && p.left_down) {
        drag_distance_ += std::hypot(p.delta.x, p.delta.y);
        if (tool_ == ViewportTool::Pan) {
            transform_.panBy(p.delta);
        } else if (roi_dragging_) {
            const Vec2 now = transform_.screenToImage(p.cursor);
            const RectD r{std::min(roi_start_image_.x, now.x), std::min(roi_start_image_.y, now.y),
                          std::fabs(now.x - roi_start_image_.x), std::fabs(now.y - roi_start_image_.y)};
            const double screen_w = r.w * transform_.zoom();
            const double screen_h = r.h * transform_.zoom();
            if (screen_w >= kRoiMinDragPx || screen_h >= kRoiMinDragPx) {
                const RectI clamped = transform_.clampToImage(r);
                if (clamped.empty()) {
                    roi_preview_.reset();
                } else {
                    roi_preview_ = clamped;
                }
            }
        }
    }

    if (dragging_ && (p.left_released || !p.left_down)) {
        if (tool_ == ViewportTool::Pan) {
            if (drag_distance_ <= kClickMaxTravelPx && transform_.hasImage()) {
                click_ = transform_.screenToImage(press_screen_);
            }
        } else if (roi_dragging_) {
            if (roi_preview_) {
                roi_ = roi_preview_;
            } else if (drag_distance_ <= kClickMaxTravelPx) {
                roi_.reset();  // a plain click in ROI mode clears the selection
            }
        }
        dragging_ = false;
        roi_dragging_ = false;
        roi_preview_.reset();
    }
}

std::optional<Vec2> ImageViewport::takeClick() {
    std::optional<Vec2> c = click_;
    click_.reset();
    return c;
}

std::optional<RectI> ImageViewport::roiForDisplay() const {
    if (roi_dragging_ && roi_preview_) return roi_preview_;
    return roi_;
}

void ImageViewport::setRoi(const RectI& roi) {
    const RectI clamped = transform_.clampToImage(
        {static_cast<double>(roi.x), static_cast<double>(roi.y), static_cast<double>(roi.w),
         static_cast<double>(roi.h)});
    if (clamped.empty()) {
        roi_.reset();
    } else {
        roi_ = clamped;
    }
}

void ImageViewport::clearRoi() {
    roi_.reset();
    roi_preview_.reset();
    roi_dragging_ = false;
}

}  // namespace optinspect::viz
