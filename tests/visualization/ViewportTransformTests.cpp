#include "visualization/ViewportTransform.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace optinspect::viz;

namespace {

ViewportTransform makeTransform(int iw, int ih, double vx, double vy, double vw, double vh) {
    ViewportTransform t;
    t.setImageSize(iw, ih);
    t.setViewport({vx, vy}, {vw, vh});
    return t;
}

}  // namespace

TEST(ViewportTransform, FitCentersAndPreservesAspectRatio) {
    // 400x200 image in an 800x800 viewport: width limits the zoom.
    ViewportTransform t = makeTransform(400, 200, 100, 50, 800, 800);
    const double expected = (800.0 - 2 * VisualizationConfig{}.fit_margin_px) / 400.0;
    EXPECT_NEAR(t.zoom(), expected, 1e-9);

    const RectD r = t.imageScreenRect();
    // Uniform scale: displayed aspect ratio equals the image aspect ratio.
    EXPECT_NEAR(r.w / r.h, 400.0 / 200.0, 1e-9);
    // Centered in the viewport.
    EXPECT_NEAR(r.x + r.w / 2.0, 100 + 400.0, 1e-9);
    EXPECT_NEAR(r.y + r.h / 2.0, 50 + 400.0, 1e-9);
}

TEST(ViewportTransform, FitUsesTheLimitingDimension) {
    ViewportTransform wide = makeTransform(100, 400, 0, 0, 800, 400);  // tall image, short viewport
    const double margin = VisualizationConfig{}.fit_margin_px;
    EXPECT_NEAR(wide.zoom(), (400.0 - 2 * margin) / 400.0, 1e-9);
}

TEST(ViewportTransform, ImageToScreenAndBackRoundTrips) {
    ViewportTransform t = makeTransform(640, 480, 20, 30, 900, 700);
    t.zoomAt({400, 300}, 1.7);
    t.panBy({25, -13});
    for (const Vec2 p : {Vec2{0, 0}, Vec2{320.5, 240.25}, Vec2{639, 479}, Vec2{-10, 900}}) {
        const Vec2 back = t.screenToImage(t.imageToScreen(p));
        EXPECT_NEAR(back.x, p.x, 1e-9);
        EXPECT_NEAR(back.y, p.y, 1e-9);
    }
}

TEST(ViewportTransform, ImageOriginIsTopLeft) {
    ViewportTransform t = makeTransform(100, 100, 10, 10, 200, 200);
    const RectD r = t.imageScreenRect();
    const Vec2 tl = t.imageToScreen({0, 0});
    EXPECT_DOUBLE_EQ(tl.x, r.x);
    EXPECT_DOUBLE_EQ(tl.y, r.y);
    // y grows downward on screen as in image space.
    EXPECT_GT(t.imageToScreen({0, 50}).y, tl.y);
    EXPECT_GT(t.imageToScreen({50, 0}).x, tl.x);
}

TEST(ViewportTransform, ZoomKeepsPointUnderCursorFixed) {
    ViewportTransform t = makeTransform(1000, 800, 0, 0, 900, 600);
    const Vec2 cursor{300, 200};
    const Vec2 before = t.screenToImage(cursor);
    t.zoomAt(cursor, 2.0);
    const Vec2 after = t.screenToImage(cursor);
    EXPECT_NEAR(before.x, after.x, 1e-9);
    EXPECT_NEAR(before.y, after.y, 1e-9);
}

TEST(ViewportTransform, ZoomIsClampedToLimits) {
    ViewportTransform t = makeTransform(1000, 1000, 0, 0, 800, 800);
    const VisualizationConfig cfg;
    for (int i = 0; i < 200; ++i) t.zoomAt({400, 400}, 2.0);
    EXPECT_DOUBLE_EQ(t.zoom(), cfg.max_zoom);
    for (int i = 0; i < 200; ++i) t.zoomAt({400, 400}, 0.5);
    EXPECT_GE(t.zoom(), t.minZoom());
    EXPECT_LE(t.zoom(), t.fitZoom() + 1e-12);  // cannot zoom out further than min(config, fit)
}

TEST(ViewportTransform, InvalidZoomFactorsAreIgnored) {
    ViewportTransform t = makeTransform(100, 100, 0, 0, 400, 400);
    const double z = t.zoom();
    t.zoomAt({10, 10}, 0.0);
    t.zoomAt({10, 10}, -2.0);
    t.zoomAt({10, 10}, std::nan(""));
    EXPECT_DOUBLE_EQ(t.zoom(), z);
}

TEST(ViewportTransform, ActualSizeIsOneToOneAndCentered) {
    ViewportTransform t = makeTransform(300, 200, 0, 0, 1000, 700);
    t.zoomAt({500, 350}, 3.0);
    t.actualSize();
    EXPECT_DOUBLE_EQ(t.zoom(), 1.0);
    const RectD r = t.imageScreenRect();
    EXPECT_NEAR(r.x + r.w / 2, 500.0, 1e-9);
    EXPECT_NEAR(r.y + r.h / 2, 350.0, 1e-9);
    EXPECT_FALSE(t.followsFit());
}

TEST(ViewportTransform, PanMovesImageByDelta) {
    ViewportTransform t = makeTransform(1000, 1000, 0, 0, 800, 800);
    t.setZoom(2.0);
    const Vec2 before = t.imageToScreen({100, 100});
    t.panBy({15, -9});
    const Vec2 after = t.imageToScreen({100, 100});
    EXPECT_NEAR(after.x - before.x, 15.0, 1e-9);
    EXPECT_NEAR(after.y - before.y, -9.0, 1e-9);
}

TEST(ViewportTransform, PanCannotLoseTheImage) {
    ViewportTransform t = makeTransform(500, 500, 0, 0, 800, 600);
    t.panBy({100000, 100000});
    RectD r = t.imageScreenRect();
    const double margin = VisualizationConfig{}.pan_margin_px;
    EXPECT_LE(r.x, 800.0 - margin + 1e-9);  // still at least `margin` px visible
    EXPECT_LE(r.y, 600.0 - margin + 1e-9);
    t.panBy({-1000000, -1000000});
    r = t.imageScreenRect();
    EXPECT_GE(r.right(), margin - 1e-9);
    EXPECT_GE(r.bottom(), margin - 1e-9);
}

TEST(ViewportTransform, ResetReturnsToFit) {
    ViewportTransform t = makeTransform(640, 480, 0, 0, 900, 700);
    const double fit = t.fitZoom();
    t.zoomAt({100, 100}, 4.0);
    t.panBy({50, 50});
    t.reset();
    EXPECT_NEAR(t.zoom(), fit, 1e-12);
    EXPECT_TRUE(t.followsFit());
}

TEST(ViewportTransform, FitFollowsViewportResizeUntilUserInteracts) {
    ViewportTransform t = makeTransform(640, 480, 0, 0, 900, 700);
    t.setViewport({0, 0}, {450, 350});
    EXPECT_NEAR(t.zoom(), t.fitZoom(), 1e-12);
    t.zoomAt({100, 100}, 1.5);
    const double z = t.zoom();
    t.setViewport({0, 0}, {450, 351});
    EXPECT_DOUBLE_EQ(t.zoom(), z);  // no longer follows fit
}

TEST(ViewportTransform, NewImageSizeRefits) {
    ViewportTransform t = makeTransform(640, 480, 0, 0, 900, 700);
    t.zoomAt({100, 100}, 3.0);
    t.setImageSize(640, 480);  // same size: keep view
    EXPECT_FALSE(t.followsFit());
    t.setImageSize(1280, 960);  // different size: fit again
    EXPECT_TRUE(t.followsFit());
}

TEST(ViewportTransform, EmptyImageIsHandled) {
    ViewportTransform t;
    t.setViewport({0, 0}, {100, 100});
    EXPECT_FALSE(t.hasImage());
    t.zoomAt({10, 10}, 2.0);
    t.panBy({5, 5});
    t.fit();
    EXPECT_FALSE(t.imageContains({10, 10}));
    EXPECT_TRUE(t.imageRectFromScreenDrag({0, 0}, {50, 50}).empty());
}

TEST(ViewportTransform, DegenerateViewportDoesNotProduceNaN) {
    ViewportTransform t;
    t.setImageSize(100, 100);
    t.setViewport({0, 0}, {0, 0});
    EXPECT_TRUE(std::isfinite(t.zoom()));
    EXPECT_TRUE(std::isfinite(t.offset().x));
}

TEST(ViewportTransform, CenterOnPutsImagePointAtViewportCenter) {
    ViewportTransform t = makeTransform(2000, 2000, 50, 40, 800, 600);
    t.setZoom(2.0);
    t.centerOn({1000, 1000});
    const Vec2 s = t.imageToScreen({1000, 1000});
    EXPECT_NEAR(s.x, 50 + 400, 1e-6);
    EXPECT_NEAR(s.y, 40 + 300, 1e-6);
}

TEST(ViewportTransform, ImageContainsRespectsImageAndViewportBounds) {
    ViewportTransform t = makeTransform(100, 100, 0, 0, 400, 400);  // image smaller than viewport
    const RectD r = t.imageScreenRect();
    EXPECT_TRUE(t.imageContains({r.x + 1, r.y + 1}));
    EXPECT_FALSE(t.imageContains({r.x - 1, r.y + 1}));
    EXPECT_FALSE(t.imageContains({r.right() + 1, r.y + 1}));
    EXPECT_FALSE(t.imageContains({-5, -5}));
}

TEST(ViewportTransform, RectConversionsAreInverse) {
    ViewportTransform t = makeTransform(640, 480, 12, 34, 900, 700);
    t.zoomAt({300, 300}, 1.9);
    const RectD image{100, 50, 40, 20};
    const RectD screen = t.imageRectToScreen(image);
    EXPECT_NEAR(screen.w, image.w * t.zoom(), 1e-9);
    EXPECT_NEAR(screen.h, image.h * t.zoom(), 1e-9);
    const RectD back = t.screenRectToImage(screen);
    EXPECT_NEAR(back.x, image.x, 1e-9);
    EXPECT_NEAR(back.y, image.y, 1e-9);
    EXPECT_NEAR(back.w, image.w, 1e-9);
    EXPECT_NEAR(back.h, image.h, 1e-9);
}

// --- ROI conversion ---------------------------------------------------------

TEST(ViewportTransform, RoiFromDragMapsScreenToImagePixels) {
    ViewportTransform t = makeTransform(100, 100, 0, 0, 100, 100);
    t.actualSize();  // 1:1, image fills the viewport exactly... with fit margin disabled by actualSize
    const RectD ir = t.imageScreenRect();
    const RectI roi = t.imageRectFromScreenDrag({ir.x + 10, ir.y + 20}, {ir.x + 40, ir.y + 60});
    EXPECT_EQ(roi, (RectI{10, 20, 30, 40}));
    EXPECT_EQ(roi.area(), 1200);
}

TEST(ViewportTransform, RoiDragOrderDoesNotMatter) {
    ViewportTransform t = makeTransform(100, 100, 0, 0, 100, 100);
    t.actualSize();
    const RectD ir = t.imageScreenRect();
    const RectI a = t.imageRectFromScreenDrag({ir.x + 10, ir.y + 20}, {ir.x + 40, ir.y + 60});
    const RectI b = t.imageRectFromScreenDrag({ir.x + 40, ir.y + 60}, {ir.x + 10, ir.y + 20});
    EXPECT_EQ(a, b);
}

TEST(ViewportTransform, RoiIsCorrectUnderZoomAndPan) {
    ViewportTransform t = makeTransform(1000, 800, 30, 20, 900, 700);
    t.zoomAt({300, 300}, 3.5);
    t.panBy({-120, 45});
    // Pick an image rectangle with integer corners, convert to screen, drag it back.
    const RectD wanted{400, 300, 80, 60};
    const RectD screen = t.imageRectToScreen(wanted);
    ASSERT_TRUE(RectD({30, 20, 900, 700}).contains({screen.x + 1, screen.y + 1})) << "test setup: ROI must be visible";
    const RectI roi = t.imageRectFromScreenDrag({screen.x, screen.y}, {screen.right(), screen.bottom()});
    // The integer corners may expand by at most one pixel because of floating point at the edges.
    EXPECT_NEAR(roi.x, 400, 1);
    EXPECT_NEAR(roi.y, 300, 1);
    EXPECT_NEAR(roi.w, 80, 2);
    EXPECT_NEAR(roi.h, 60, 2);
}

TEST(ViewportTransform, RoiIsClampedToTheImage) {
    ViewportTransform t = makeTransform(100, 100, 0, 0, 400, 400);
    const RectD ir = t.imageScreenRect();
    const RectI roi = t.imageRectFromScreenDrag({ir.x - 50, ir.y - 50}, {ir.right() + 50, ir.bottom() + 50});
    EXPECT_EQ(roi, (RectI{0, 0, 100, 100}));
}

TEST(ViewportTransform, RoiOutsideTheImageIsEmpty) {
    ViewportTransform t = makeTransform(100, 100, 0, 0, 400, 400);
    const RectD ir = t.imageScreenRect();
    EXPECT_TRUE(t.imageRectFromScreenDrag({ir.right() + 10, ir.y}, {ir.right() + 60, ir.y + 40}).empty());
}

TEST(ViewportTransform, ClampToImageRoundsOutwardToWholePixels) {
    ViewportTransform t = makeTransform(100, 100, 0, 0, 400, 400);
    const RectI r = t.clampToImage({10.4, 20.6, 5.2, 3.1});
    EXPECT_EQ(r.x, 10);
    EXPECT_EQ(r.y, 20);
    EXPECT_EQ(r.x + r.w, 16);  // ceil(15.6)
    EXPECT_EQ(r.y + r.h, 24);  // ceil(23.7)
}

// --- GL viewport conversion --------------------------------------------------

TEST(GlViewportRect, FlipsYToBottomLeftOrigin) {
    // 1080 px high framebuffer, panel at screen y=100 with height 300 (scale 1).
    const RectI vp = toGlViewportRect({50, 100, 400, 300}, 1.0, 1.0, 1080);
    EXPECT_EQ(vp.x, 50);
    EXPECT_EQ(vp.w, 400);
    EXPECT_EQ(vp.h, 300);
    EXPECT_EQ(vp.y, 1080 - (100 + 300));
}

TEST(GlViewportRect, AppliesHighDpiScale) {
    const RectI vp = toGlViewportRect({10, 20, 100, 50}, 2.0, 2.0, 1000);
    EXPECT_EQ(vp, (RectI{20, 1000 - (40 + 100), 200, 100}));
}
