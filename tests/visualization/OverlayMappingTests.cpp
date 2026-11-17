#include "visualization/DefectOverlayRenderer.hpp"
#include "visualization/ImageViewport.hpp"

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

// Vertices of the i-th quad (6 vertices) of the geometry as screen-local rectangles.
RectD quadBounds(const OverlayGeometry& g, std::size_t quad) {
    const int stride = OverlayGeometry::kFloatsPerVertex;
    double min_x = 1e18, min_y = 1e18, max_x = -1e18, max_y = -1e18;
    for (std::size_t v = quad * 6; v < quad * 6 + 6; ++v) {
        const double x = g.vertices[v * stride];
        const double y = g.vertices[v * stride + 1];
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    }
    return {min_x, min_y, max_x - min_x, max_y - min_y};
}

}  // namespace

TEST(OverlayMapping, BoundingBoxMapsThroughZoomAndPan) {
    ViewportTransform t = makeTransform(1000, 1000, 100, 50, 800, 800);
    t.zoomAt({400, 300}, 2.5);
    t.panBy({-40, 25});

    const DefectBox box{{300, 400, 60, 40}, 1, 2400.0};
    const RectD expected_screen = t.imageRectToScreen(box.image_rect);

    OverlayGeometry g;
    OverlayStyle style;
    ASSERT_TRUE(appendRect(g, t, box.image_rect, style.defect_outline, 2.0f));

    // Outline = 4 bars; the union of their bounds is the box grown by half the thickness.
    double min_x = 1e18, min_y = 1e18, max_x = -1e18, max_y = -1e18;
    for (std::size_t q = 0; q < 4; ++q) {
        const RectD b = quadBounds(g, q);
        min_x = std::min(min_x, b.x);
        min_y = std::min(min_y, b.y);
        max_x = std::max(max_x, b.right());
        max_y = std::max(max_y, b.bottom());
    }
    // Vertices are viewport-local: add the viewport origin to compare with screen space.
    EXPECT_NEAR(min_x + 100 + 1.0, expected_screen.x, 1e-3);
    EXPECT_NEAR(min_y + 50 + 1.0, expected_screen.y, 1e-3);
    EXPECT_NEAR(max_x + 100 - 1.0, expected_screen.right(), 1e-3);
    EXPECT_NEAR(max_y + 50 - 1.0, expected_screen.bottom(), 1e-3);
}

TEST(OverlayMapping, OverlayStaysAlignedWithImagePixelsWhenZooming) {
    // The screen position of an image pixel and of a box corner at that pixel must agree
    // at every zoom/pan state, otherwise overlays would drift against the texture.
    ViewportTransform t = makeTransform(640, 640, 0, 0, 900, 700);
    const RectD box{200, 300, 50, 50};
    for (double factor : {1.0, 1.7, 0.6, 3.3}) {
        t.zoomAt({450, 350}, factor);
        t.panBy({13, -7});
        const Vec2 pixel_corner = t.imageToScreen({box.x, box.y});
        const RectD screen_box = t.imageRectToScreen(box);
        EXPECT_NEAR(pixel_corner.x, screen_box.x, 1e-9);
        EXPECT_NEAR(pixel_corner.y, screen_box.y, 1e-9);
        EXPECT_NEAR(screen_box.w, box.w * t.zoom(), 1e-9);
    }
}

TEST(OverlayMapping, EveryVisibleDefectProducesOneOutlineOfFourQuads) {
    ViewportTransform t = makeTransform(640, 640, 0, 0, 640, 640);
    t.actualSize();
    std::vector<DefectBox> defects = {{{10, 10, 30, 30}, 1, 900}, {{100, 120, 20, 40}, 2, 800}};
    const OverlayGeometry g = buildDefectOverlay(defects, std::nullopt, t, OverlayStyle{});
    EXPECT_EQ(g.shape_count, 2u);
    EXPECT_EQ(g.vertexCount(), 2u * 4u * 6u);
    ASSERT_EQ(g.labels.size(), 2u);
    EXPECT_EQ(g.labels[0].index, 1);
    EXPECT_FALSE(g.labels[0].selected);
}

TEST(OverlayMapping, SelectedDefectUsesHighlightStyleAndIsDrawnLast) {
    ViewportTransform t = makeTransform(640, 640, 0, 0, 640, 640);
    t.actualSize();
    std::vector<DefectBox> defects = {{{10, 10, 30, 30}, 1, 900}, {{100, 120, 20, 40}, 2, 800}, {{300, 300, 25, 25}, 3, 600}};
    const OverlayStyle style;
    const OverlayGeometry g = buildDefectOverlay(defects, 1, t, style);

    // 2 ordinary outlines (4 quads each) + selected fill (1) + selected outline (4).
    EXPECT_EQ(g.vertexCount(), (2u * 4u + 1u + 4u) * 6u);
    ASSERT_EQ(g.labels.size(), 3u);
    EXPECT_TRUE(g.labels.back().selected);
    EXPECT_EQ(g.labels.back().index, 1);

    // The last quad uses the selected outline colour, the first the ordinary one.
    const int stride = OverlayGeometry::kFloatsPerVertex;
    const std::size_t last = g.vertexCount() - 1;
    EXPECT_FLOAT_EQ(g.vertices[last * stride + 2], style.selected_outline.r);
    EXPECT_FLOAT_EQ(g.vertices[last * stride + 3], style.selected_outline.g);
    EXPECT_FLOAT_EQ(g.vertices[2], style.defect_outline.r);
    EXPECT_FLOAT_EQ(g.vertices[3], style.defect_outline.g);
}

TEST(OverlayMapping, SelectedOutlineIsThickerThanOrdinary) {
    ViewportTransform t = makeTransform(640, 640, 0, 0, 640, 640);
    t.actualSize();
    const OverlayStyle style;
    OverlayGeometry a, b;
    appendRect(a, t, {100, 100, 60, 60}, style.defect_outline, style.defect_thickness_px);
    appendRect(b, t, {100, 100, 60, 60}, style.selected_outline, style.selected_thickness_px);
    EXPECT_GT(quadBounds(b, 0).h, quadBounds(a, 0).h);  // top bar height = thickness
}

TEST(OverlayMapping, ShapesOutsideTheViewportAreCulled) {
    ViewportTransform t = makeTransform(2000, 2000, 0, 0, 400, 400);
    t.actualSize();  // only a 400x400 window of the image is visible
    const OverlayGeometry g = buildDefectOverlay({{{1500, 1500, 30, 30}, 1, 900}}, std::nullopt, t, OverlayStyle{});
    EXPECT_EQ(g.shape_count, 0u);
    EXPECT_TRUE(g.vertices.empty());
    EXPECT_TRUE(g.labels.empty());
}

TEST(OverlayMapping, TinyBoxesAreInflatedToAMinimumScreenSize) {
    ViewportTransform t = makeTransform(4000, 4000, 0, 0, 400, 400);  // zoom ~0.09: a 6 px box is ~0.5 px
    OverlayGeometry g;
    OverlayStyle style;
    ASSERT_TRUE(appendRect(g, t, {2000, 2000, 6, 6}, style.defect_outline, 2.0f, nullptr, style.min_box_px));
    const RectD top = quadBounds(g, 0);
    EXPECT_GE(top.w, style.min_box_px);  // width of the top bar spans at least the minimum box
}

TEST(OverlayMapping, ZeroSizedAndNegativeRectsAreHandled) {
    ViewportTransform t = makeTransform(100, 100, 0, 0, 200, 200);
    OverlayGeometry g;
    EXPECT_FALSE(appendRect(g, t, {10, 10, -5, 5}, Color4{}, 2.0f));
    EXPECT_TRUE(g.vertices.empty());
    EXPECT_TRUE(appendRect(g, t, {10, 10, 0, 0}, Color4{}, 2.0f, nullptr, 4.0f));
}

TEST(OverlayMapping, RoiIsDrawnWithFillAndOutline) {
    ViewportTransform t = makeTransform(640, 640, 0, 0, 640, 640);
    t.actualSize();
    OverlayGeometry g;
    appendRoi(g, t, {50, 60, 100, 80}, OverlayStyle{});
    EXPECT_EQ(g.vertexCount(), (1u + 4u) * 6u);
    EXPECT_EQ(g.shape_count, 1u);
    EXPECT_TRUE(g.labels.empty());  // ROI has no label
}

TEST(OverlayMapping, LabelAnchorFollowsTheBoxTopLeft) {
    ViewportTransform t = makeTransform(640, 640, 20, 10, 800, 600);
    t.zoomAt({300, 300}, 2.0);
    const OverlayGeometry g = buildDefectOverlay({{{120, 130, 20, 20}, 7, 400}}, std::nullopt, t, OverlayStyle{});
    ASSERT_EQ(g.labels.size(), 1u);
    const Vec2 expected = t.imageToScreen({120, 130});
    EXPECT_NEAR(g.labels[0].screen_pos.x, expected.x, 1e-9);
    EXPECT_NEAR(g.labels[0].screen_pos.y, expected.y, 1e-9);
    EXPECT_EQ(g.labels[0].index, 7);
    EXPECT_DOUBLE_EQ(g.labels[0].area_px, 400.0);
}

// --- hit testing ----------------------------------------------------------------

TEST(HitTest, FindsTheDefectUnderThePoint) {
    const std::vector<DefectBox> defects = {{{10, 10, 30, 30}, 1, 900}, {{100, 100, 20, 20}, 2, 400}};
    EXPECT_EQ(hitTestDefects(defects, {20, 20}), std::optional<int>(1));
    EXPECT_EQ(hitTestDefects(defects, {110, 110}), std::optional<int>(2));
    EXPECT_EQ(hitTestDefects(defects, {60, 60}), std::nullopt);
}

TEST(HitTest, ToleranceLetsTinyBoxesBeSelected) {
    const std::vector<DefectBox> defects = {{{50, 50, 2, 2}, 4, 4}};
    EXPECT_EQ(hitTestDefects(defects, {55, 51}), std::nullopt);
    EXPECT_EQ(hitTestDefects(defects, {55, 51}, 4.0), std::optional<int>(4));
}

TEST(HitTest, SmallestBoxWinsWhenBoxesOverlap) {
    const std::vector<DefectBox> defects = {{{0, 0, 100, 100}, 1, 10000}, {{40, 40, 10, 10}, 2, 100}};
    EXPECT_EQ(hitTestDefects(defects, {45, 45}), std::optional<int>(2));
}

// --- ImageViewport interaction (pure logic, no OpenGL) ------------------------------

namespace {

PointerState move(double x, double y) {
    PointerState p;
    p.cursor = {x, y};
    p.hovered = true;
    return p;
}

}  // namespace

TEST(ImageViewport, CursorImagePositionMatchesTransform) {
    ImageViewport v;
    v.transform().setImageSize(200, 100);
    v.transform().setViewport({0, 0}, {400, 300});
    v.update(move(200, 150));
    ASSERT_TRUE(v.cursorImagePosition().has_value());
    const Vec2 expected = v.transform().screenToImage({200, 150});
    EXPECT_DOUBLE_EQ(v.cursorImagePosition()->x, expected.x);
    EXPECT_DOUBLE_EQ(v.cursorImagePosition()->y, expected.y);

    v.update(move(2, 2));  // viewport corner, outside the fitted image
    EXPECT_FALSE(v.cursorImagePosition().has_value());
}

TEST(ImageViewport, WheelZoomsAroundTheCursor) {
    ImageViewport v;
    v.transform().setImageSize(1000, 1000);
    v.transform().setViewport({0, 0}, {800, 800});
    const Vec2 cursor{300, 250};
    const Vec2 before = v.transform().screenToImage(cursor);
    PointerState p = move(cursor.x, cursor.y);
    p.wheel = 3.0;
    v.update(p);
    EXPECT_GT(v.transform().zoom(), v.transform().fitZoom());
    const Vec2 after = v.transform().screenToImage(cursor);
    EXPECT_NEAR(before.x, after.x, 1e-9);
    EXPECT_NEAR(before.y, after.y, 1e-9);
}

TEST(ImageViewport, LeftDragPansInPanTool) {
    ImageViewport v;
    v.transform().setImageSize(1000, 1000);
    v.transform().setViewport({0, 0}, {800, 800});
    v.transform().setZoom(2.0);
    const Vec2 before = v.transform().offset();

    PointerState press = move(400, 400);
    press.left_pressed = press.left_down = true;
    v.update(press);
    PointerState drag = move(430, 390);
    drag.left_down = true;
    drag.delta = {30, -10};
    v.update(drag);
    PointerState release = move(430, 390);
    release.left_released = true;
    v.update(release);

    EXPECT_NEAR(v.transform().offset().x - before.x, 30.0, 1e-9);
    EXPECT_NEAR(v.transform().offset().y - before.y, -10.0, 1e-9);
    EXPECT_FALSE(v.takeClick().has_value());  // it was a drag, not a click
}

TEST(ImageViewport, ClickWithoutMovementIsReportedOnce) {
    ImageViewport v;
    v.transform().setImageSize(400, 400);
    v.transform().setViewport({0, 0}, {400, 400});
    PointerState press = move(200, 200);
    press.left_pressed = press.left_down = true;
    v.update(press);
    PointerState release = move(200, 200);
    release.left_released = true;
    v.update(release);
    const std::optional<Vec2> click = v.takeClick();
    ASSERT_TRUE(click.has_value());
    EXPECT_NEAR(click->x, v.transform().screenToImage({200, 200}).x, 1e-9);
    EXPECT_FALSE(v.takeClick().has_value());
}

TEST(ImageViewport, RoiToolSelectsRectangleInImagePixels) {
    ImageViewport v;
    v.transform().setImageSize(100, 100);
    v.transform().setViewport({0, 0}, {100, 100});
    v.transform().actualSize();
    v.setTool(ViewportTool::Roi);

    PointerState press = move(10, 20);
    press.left_pressed = press.left_down = true;
    v.update(press);
    EXPECT_TRUE(v.roiDragActive());
    PointerState drag = move(40, 60);
    drag.left_down = true;
    drag.delta = {30, 40};
    v.update(drag);
    ASSERT_TRUE(v.roiForDisplay().has_value());
    EXPECT_FALSE(v.roi().has_value());  // not committed yet
    PointerState release = move(40, 60);
    release.left_released = true;
    v.update(release);

    ASSERT_TRUE(v.roi().has_value());
    EXPECT_EQ(*v.roi(), (RectI{10, 20, 30, 40}));
    EXPECT_FALSE(v.roiDragActive());
}

TEST(ImageViewport, RoiStaysAttachedToTheImageWhenZooming) {
    ImageViewport v;
    v.transform().setImageSize(100, 100);
    v.transform().setViewport({0, 0}, {100, 100});
    v.transform().actualSize();
    v.setRoi({10, 20, 30, 40});
    v.transform().zoomAt({50, 50}, 2.0);
    ASSERT_TRUE(v.roi().has_value());
    EXPECT_EQ(*v.roi(), (RectI{10, 20, 30, 40}));  // image pixels do not change with the view
}

TEST(ImageViewport, PlainClickInRoiToolClearsTheRoi) {
    ImageViewport v;
    v.transform().setImageSize(100, 100);
    v.transform().setViewport({0, 0}, {100, 100});
    v.transform().actualSize();
    v.setTool(ViewportTool::Roi);
    v.setRoi({10, 10, 20, 20});
    PointerState press = move(50, 50);
    press.left_pressed = press.left_down = true;
    v.update(press);
    PointerState release = move(50, 50);
    release.left_released = true;
    v.update(release);
    EXPECT_FALSE(v.roi().has_value());
}

TEST(ImageViewport, SetRoiIsClampedToTheImage) {
    ImageViewport v;
    v.transform().setImageSize(100, 100);
    v.transform().setViewport({0, 0}, {200, 200});
    v.setRoi({80, 80, 100, 100});
    ASSERT_TRUE(v.roi().has_value());
    EXPECT_EQ(*v.roi(), (RectI{80, 80, 20, 20}));
    v.setRoi({500, 500, 10, 10});
    EXPECT_FALSE(v.roi().has_value());
}

TEST(ImageViewport, MiddleButtonPansInAnyTool) {
    ImageViewport v;
    v.transform().setImageSize(1000, 1000);
    v.transform().setViewport({0, 0}, {800, 800});
    v.transform().setZoom(2.0);
    v.setTool(ViewportTool::Roi);
    const Vec2 before = v.transform().offset();
    PointerState p = move(300, 300);
    p.middle_down = true;
    p.delta = {12, 5};
    v.update(p);
    EXPECT_NEAR(v.transform().offset().x - before.x, 12.0, 1e-9);
    EXPECT_NEAR(v.transform().offset().y - before.y, 5.0, 1e-9);
}
