#include "visualization/ViewportTransform.hpp"

#include <algorithm>
#include <cmath>

namespace optinspect::viz {

ViewportTransform::ViewportTransform(const VisualizationConfig& config) : config_(config) {}

void ViewportTransform::setImageSize(int width, int height) {
    width = std::max(width, 0);
    height = std::max(height, 0);
    if (width == image_w_ && height == image_h_) return;
    image_w_ = width;
    image_h_ = height;
    fit();
}

void ViewportTransform::setViewport(Vec2 origin, Vec2 size) {
    origin_ = origin;
    size_ = {std::max(size.x, 0.0), std::max(size.y, 0.0)};
    if (follow_fit_) {
        applyFitIfFollowing();
    } else {
        clampPan();
    }
}

double ViewportTransform::fitZoom() const {
    if (!hasImage() || size_.x <= 0.0 || size_.y <= 0.0) return 1.0;
    const double avail_w = std::max(size_.x - 2.0 * config_.fit_margin_px, 1.0);
    const double avail_h = std::max(size_.y - 2.0 * config_.fit_margin_px, 1.0);
    const double z = std::min(avail_w / image_w_, avail_h / image_h_);
    return std::min(z, config_.max_zoom);
}

double ViewportTransform::minZoom() const {
    // Never forbid the zoom that fit() itself would choose.
    return std::min(config_.min_zoom, fitZoom());
}

void ViewportTransform::applyFitIfFollowing() {
    if (!hasImage()) return;
    zoom_ = fitZoom();
    offset_ = {(size_.x - image_w_ * zoom_) * 0.5, (size_.y - image_h_ * zoom_) * 0.5};
}

void ViewportTransform::fit() {
    follow_fit_ = true;
    applyFitIfFollowing();
}

void ViewportTransform::actualSize() {
    if (!hasImage()) return;
    follow_fit_ = false;
    zoom_ = std::clamp(1.0, minZoom(), config_.max_zoom);
    offset_ = {(size_.x - image_w_ * zoom_) * 0.5, (size_.y - image_h_ * zoom_) * 0.5};
    clampPan();
}

void ViewportTransform::zoomAt(Vec2 screen_point, double factor) {
    if (!hasImage() || factor <= 0.0 || !std::isfinite(factor)) return;
    const Vec2 image_point = screenToImage(screen_point);
    const double new_zoom = std::clamp(zoom_ * factor, minZoom(), config_.max_zoom);
    if (new_zoom == zoom_) return;
    follow_fit_ = false;
    zoom_ = new_zoom;
    const Vec2 local = screen_point - origin_;
    offset_ = {local.x - image_point.x * zoom_, local.y - image_point.y * zoom_};
    clampPan();
}

void ViewportTransform::setZoom(double zoom) {
    if (!hasImage() || zoom <= 0.0 || !std::isfinite(zoom)) return;
    zoomAt({origin_.x + size_.x * 0.5, origin_.y + size_.y * 0.5}, zoom / zoom_);
}

void ViewportTransform::panBy(Vec2 screen_delta) {
    if (!hasImage()) return;
    follow_fit_ = false;
    offset_ = offset_ + screen_delta;
    clampPan();
}

void ViewportTransform::centerOn(Vec2 image_point) {
    if (!hasImage()) return;
    follow_fit_ = false;
    offset_ = {size_.x * 0.5 - image_point.x * zoom_, size_.y * 0.5 - image_point.y * zoom_};
    clampPan();
}

void ViewportTransform::copyViewFrom(const ViewportTransform& other) {
    zoom_ = other.zoom_;
    offset_ = other.offset_;
    follow_fit_ = other.follow_fit_;
    if (follow_fit_) {
        applyFitIfFollowing();
    } else {
        clampPan();
    }
}

void ViewportTransform::clampPan() {
    if (!hasImage()) return;
    const double img_w = image_w_ * zoom_;
    const double img_h = image_h_ * zoom_;
    // At least `margin` pixels of the image (or all of it, if smaller) must
    // remain inside the viewport so it can never be lost off-screen.
    const double margin_x = std::min({static_cast<double>(config_.pan_margin_px), img_w, size_.x});
    const double margin_y = std::min({static_cast<double>(config_.pan_margin_px), img_h, size_.y});
    offset_.x = std::clamp(offset_.x, margin_x - img_w, size_.x - margin_x);
    offset_.y = std::clamp(offset_.y, margin_y - img_h, size_.y - margin_y);
}

Vec2 ViewportTransform::imageToScreen(Vec2 p) const {
    return {origin_.x + offset_.x + p.x * zoom_, origin_.y + offset_.y + p.y * zoom_};
}

Vec2 ViewportTransform::screenToImage(Vec2 p) const {
    return {(p.x - origin_.x - offset_.x) / zoom_, (p.y - origin_.y - offset_.y) / zoom_};
}

RectD ViewportTransform::imageRectToScreen(const RectD& r) const {
    const Vec2 tl = imageToScreen({r.x, r.y});
    return {tl.x, tl.y, r.w * zoom_, r.h * zoom_};
}

RectD ViewportTransform::screenRectToImage(const RectD& r) const {
    const Vec2 tl = screenToImage({r.x, r.y});
    return {tl.x, tl.y, r.w / zoom_, r.h / zoom_};
}

RectD ViewportTransform::imageScreenRect() const {
    return imageRectToScreen({0.0, 0.0, static_cast<double>(image_w_), static_cast<double>(image_h_)});
}

bool ViewportTransform::viewportContains(Vec2 p) const {
    return RectD{origin_.x, origin_.y, size_.x, size_.y}.contains(p);
}

bool ViewportTransform::imageContains(Vec2 p) const {
    return hasImage() && viewportContains(p) && imageScreenRect().contains(p);
}

RectI ViewportTransform::clampToImage(const RectD& r) const {
    if (!hasImage() || r.w < 0.0 || r.h < 0.0) return {};
    const double x0 = std::clamp(std::floor(r.x), 0.0, static_cast<double>(image_w_));
    const double y0 = std::clamp(std::floor(r.y), 0.0, static_cast<double>(image_h_));
    const double x1 = std::clamp(std::ceil(r.right()), 0.0, static_cast<double>(image_w_));
    const double y1 = std::clamp(std::ceil(r.bottom()), 0.0, static_cast<double>(image_h_));
    if (x1 <= x0 || y1 <= y0) return {};
    return {static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1 - x0),
            static_cast<int>(y1 - y0)};
}

RectI ViewportTransform::imageRectFromScreenDrag(Vec2 a, Vec2 b) const {
    const Vec2 ia = screenToImage(a);
    const Vec2 ib = screenToImage(b);
    const RectD r{std::min(ia.x, ib.x), std::min(ia.y, ib.y), std::fabs(ia.x - ib.x),
                  std::fabs(ia.y - ib.y)};
    return clampToImage(r);
}

RectI toGlViewportRect(const RectD& r, double scale_x, double scale_y, int framebuffer_height) {
    const int x = static_cast<int>(std::lround(r.x * scale_x));
    const int w = static_cast<int>(std::lround(r.w * scale_x));
    const int h = static_cast<int>(std::lround(r.h * scale_y));
    const int top = static_cast<int>(std::lround(r.y * scale_y));
    return {x, framebuffer_height - (top + h), w, h};
}

}  // namespace optinspect::viz
