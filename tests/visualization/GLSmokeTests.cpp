// OpenGL smoke tests. They need a real OpenGL 3.3 core context; when the machine cannot
// provide one (no GPU driver, headless CI) every test is SKIPPED - never reported as passed.
#include "visualization/DefectOverlayRenderer.hpp"
#include "visualization/GLContext.hpp"
#include "visualization/ImageRenderer.hpp"
#include "visualization/Shader.hpp"
#include "visualization/Texture2D.hpp"

#include <glad/glad.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

using namespace optinspect::viz;

namespace {

std::filesystem::path shaderDir() {
    std::filesystem::path p(OPTINSPECT_SHADER_SOURCE_DIR);
    return p;
}

struct Rgba {
    unsigned char r, g, b, a;
};

/// Offscreen render target so the tests do not depend on a visible window's pixel ownership.
class Offscreen {
public:
    explicit Offscreen(int size) : size_(size) {
        glGenTextures(1, &color_);
        glBindTexture(GL_TEXTURE_2D, color_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
        complete_ = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    ~Offscreen() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo_);
        glDeleteTextures(1, &color_);
    }
    bool complete() const { return complete_; }
    int size() const { return size_; }
    /// Pixel at screen coordinates (origin top-left), as the viewer would show it.
    Rgba pixelAtScreen(int x, int y) const {
        Rgba px{};
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, size_ - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &px);
        return px;
    }

private:
    int size_;
    unsigned int color_ = 0;
    unsigned int fbo_ = 0;
    bool complete_ = false;
};

}  // namespace

class GlSmokeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        try {
            GLContextOptions options;
            options.visible = false;
            options.width = 128;
            options.height = 128;
            options.vsync = false;
            context_ = std::make_unique<GLContext>(GLContext::create(options));
        } catch (const std::exception& e) {
            skip_reason_ = e.what();
        }
    }
    static void TearDownTestSuite() { context_.reset(); }
    void SetUp() override {
        if (!context_) GTEST_SKIP() << "No OpenGL 3.3 core context available: " << skip_reason_;
    }

    static std::unique_ptr<GLContext> context_;
    static std::string skip_reason_;
};

std::unique_ptr<GLContext> GlSmokeTest::context_;
std::string GlSmokeTest::skip_reason_;

TEST_F(GlSmokeTest, ContextIsCoreProfile33OrNewerAndGladLoaded) {
    EXPECT_GE(context_->info().major, 3);
    if (context_->info().major == 3) EXPECT_GE(context_->info().minor, 3);
    EXPECT_FALSE(context_->info().renderer.empty());
    EXPECT_GT(context_->info().max_texture_size, 0);
    GLint profile = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    EXPECT_TRUE(profile & GL_CONTEXT_CORE_PROFILE_BIT);
    std::cout << "[ INFO     ] " << context_->info().version << " | " << context_->info().renderer << "\n";
}

TEST_F(GlSmokeTest, ProjectShadersCompileAndLink) {
    Shader image = Shader::fromFiles(shaderDir() / "image.vert", shaderDir() / "image.frag");
    EXPECT_NE(image.id(), 0u);
    Shader overlay = Shader::fromFiles(shaderDir() / "overlay.vert", shaderDir() / "overlay.frag");
    EXPECT_NE(overlay.id(), 0u);
    image.use();
    image.setInt("uMode", 0);
    EXPECT_THROW(image.setInt("doesNotExist", 1), ShaderError);
}

TEST_F(GlSmokeTest, BrokenShaderReportsTheCompilerLog) {
    const auto dir = std::filesystem::temp_directory_path() / "optinspect_smoke_shader";
    std::filesystem::create_directories(dir);
    {
        std::ofstream(dir / "bad.vert") << "#version 330 core\nvoid main() { this is not glsl }\n";
        std::ofstream(dir / "bad.frag") << "#version 330 core\nout vec4 c;\nvoid main(){ c = vec4(1.0); }\n";
    }
    try {
        Shader::fromFiles(dir / "bad.vert", dir / "bad.frag");
        FAIL() << "expected ShaderError";
    } catch (const ShaderError& e) {
        EXPECT_NE(std::string(e.what()).find("bad.vert"), std::string::npos);
    }
    EXPECT_THROW(Shader::fromFiles(dir / "missing.vert", dir / "bad.frag"), ShaderError);
    std::filesystem::remove_all(dir);
}

TEST_F(GlSmokeTest, TexturesAreCreatedForGrayBgrAndBgra) {
    Texture2D gray, bgr, bgra;
    gray.upload(cv::Mat(5, 7, CV_8UC1, cv::Scalar(10)));
    bgr.upload(cv::Mat(5, 7, CV_8UC3, cv::Scalar(1, 2, 3)));
    bgra.upload(cv::Mat(5, 7, CV_8UC4, cv::Scalar(1, 2, 3, 4)));
    EXPECT_TRUE(gray.valid() && bgr.valid() && bgra.valid());
    EXPECT_EQ(gray.width(), 7);
    EXPECT_EQ(bgr.height(), 5);
    EXPECT_GT(bgra.gpuBytes(), bgr.gpuBytes());
    EXPECT_THROW(Texture2D().upload(cv::Mat()), TextureError);
    EXPECT_THROW(Texture2D().upload(cv::Mat(4, 4, CV_16UC1)), TextureError);
}

TEST_F(GlSmokeTest, ReplacingContentWithSameSizeDoesNotReallocate) {
    Texture2D t;
    t.upload(cv::Mat(16, 16, CV_8UC3, cv::Scalar(0)));
    t.upload(cv::Mat(16, 16, CV_8UC3, cv::Scalar(255)));
    t.upload(cv::Mat(16, 16, CV_8UC3, cv::Scalar(100)));
    EXPECT_EQ(t.allocationCount(), 1u);
    EXPECT_EQ(t.updateCount(), 2u);
    t.upload(cv::Mat(32, 16, CV_8UC3, cv::Scalar(100)));  // size change: new storage
    EXPECT_EQ(t.allocationCount(), 2u);
    t.upload(cv::Mat(32, 16, CV_8UC1, cv::Scalar(100)));  // layout change: new storage
    EXPECT_EQ(t.allocationCount(), 3u);
}

namespace {

// 2x2 image: top-left red, top-right green, bottom-left blue, bottom-right white (BGR order in memory).
cv::Mat makeQuadrants() {
    cv::Mat m(2, 2, CV_8UC3);
    m.at<cv::Vec3b>(0, 0) = {0, 0, 255};
    m.at<cv::Vec3b>(0, 1) = {0, 255, 0};
    m.at<cv::Vec3b>(1, 0) = {255, 0, 0};
    m.at<cv::Vec3b>(1, 1) = {255, 255, 255};
    return m;
}

void renderFilled(ImageRenderer& renderer, Texture2D& texture, int image_w, int image_h, int target) {
    VisualizationConfig cfg;
    cfg.fit_margin_px = 0.0;
    ViewportTransform t(cfg);
    t.setImageSize(image_w, image_h);
    t.setViewport({0, 0}, {static_cast<double>(target), static_cast<double>(target)});
    t.fit();
    renderer.beginViewport(toGlViewportRect({0, 0, static_cast<double>(target), static_cast<double>(target)}, 1, 1, target),
                           {0, 0, 0, 1});
    renderer.drawImage(texture, t);
    renderer.endViewport();
}

}  // namespace

TEST_F(GlSmokeTest, BgrImageRendersWithCorrectColorsAndOrientation) {
    Offscreen target(64);
    ASSERT_TRUE(target.complete());
    VisualizationConfig cfg;
    cfg.fit_margin_px = 0.0;
    ImageRenderer renderer(shaderDir(), cfg);
    Texture2D texture;
    texture.upload(makeQuadrants(), false);
    renderFilled(renderer, texture, 2, 2, 64);

    const Rgba tl = target.pixelAtScreen(16, 16);
    const Rgba tr = target.pixelAtScreen(48, 16);
    const Rgba bl = target.pixelAtScreen(16, 48);
    const Rgba br = target.pixelAtScreen(48, 48);
    EXPECT_EQ(tl.r, 255); EXPECT_EQ(tl.g, 0);   EXPECT_EQ(tl.b, 0);    // red: BGR was swapped to RGB, row 0 is on top
    EXPECT_EQ(tr.r, 0);   EXPECT_EQ(tr.g, 255); EXPECT_EQ(tr.b, 0);
    EXPECT_EQ(bl.r, 0);   EXPECT_EQ(bl.g, 0);   EXPECT_EQ(bl.b, 255);
    EXPECT_EQ(br.r, 255); EXPECT_EQ(br.g, 255); EXPECT_EQ(br.b, 255);
}

TEST_F(GlSmokeTest, GrayImageRendersAsNeutralGray) {
    Offscreen target(64);
    ASSERT_TRUE(target.complete());
    VisualizationConfig cfg;
    cfg.fit_margin_px = 0.0;
    ImageRenderer renderer(shaderDir(), cfg);
    cv::Mat gray(2, 2, CV_8UC1);
    gray.at<uchar>(0, 0) = 0;
    gray.at<uchar>(0, 1) = 255;
    gray.at<uchar>(1, 0) = 128;
    gray.at<uchar>(1, 1) = 64;
    Texture2D texture;
    texture.upload(gray, false);
    renderFilled(renderer, texture, 2, 2, 64);

    const Rgba tr = target.pixelAtScreen(48, 16);
    EXPECT_EQ(tr.r, 255); EXPECT_EQ(tr.g, 255); EXPECT_EQ(tr.b, 255);
    const Rgba bl = target.pixelAtScreen(16, 48);
    EXPECT_NEAR(bl.r, 128, 1); EXPECT_EQ(bl.r, bl.g); EXPECT_EQ(bl.g, bl.b);
}

TEST_F(GlSmokeTest, ImageWithOddWidthAndPaddedRowsUploadsWithoutShear) {
    Offscreen target(63);
    ASSERT_TRUE(target.complete());
    VisualizationConfig cfg;
    cfg.fit_margin_px = 0.0;
    ImageRenderer renderer(shaderDir(), cfg);

    // 3 px wide (row = 9 bytes, not a multiple of 4) taken as a sub-matrix of a wider image.
    cv::Mat big(3, 8, CV_8UC3, cv::Scalar(0, 0, 0));
    big.at<cv::Vec3b>(0, 2) = {0, 0, 255};     // inside the ROI, top-right
    big.at<cv::Vec3b>(2, 1) = {255, 0, 0};     // inside the ROI, bottom-middle
    const cv::Mat roi = big(cv::Rect(0, 0, 3, 3));
    Texture2D texture;
    texture.upload(roi, false);
    renderFilled(renderer, texture, 3, 3, 63);

    // 63 px / 3 = 21 px per image pixel. Top-right pixel centre ~ (52, 10); bottom-middle ~ (31, 52).
    const Rgba red = target.pixelAtScreen(52, 10);
    EXPECT_EQ(red.r, 255); EXPECT_EQ(red.b, 0);
    const Rgba blue = target.pixelAtScreen(31, 52);
    EXPECT_EQ(blue.b, 255); EXPECT_EQ(blue.r, 0);
}

TEST_F(GlSmokeTest, OverlayIsDrawnOnTopOfTheImage) {
    Offscreen target(64);
    ASSERT_TRUE(target.complete());
    VisualizationConfig cfg;
    cfg.fit_margin_px = 0.0;
    ImageRenderer renderer(shaderDir(), cfg);
    DefectOverlayRenderer overlay(shaderDir());
    Texture2D texture;
    texture.upload(cv::Mat(64, 64, CV_8UC3, cv::Scalar(0, 0, 0)), false);

    ViewportTransform t(cfg);
    t.setImageSize(64, 64);
    t.setViewport({0, 0}, {64, 64});
    t.fit();
    OverlayStyle style;
    style.defect_outline = {1, 0, 0, 1};
    const OverlayGeometry g = buildDefectOverlay({{{16, 16, 32, 32}, 1, 1024}}, std::nullopt, t, style);
    ASSERT_GT(g.vertexCount(), 0u);

    renderer.beginViewport(toGlViewportRect({0, 0, 64, 64}, 1, 1, 64), {0, 0, 0, 1});
    renderer.drawImage(texture, t);
    overlay.render(g, t.viewportSize());
    renderer.endViewport();

    const Rgba on_edge = target.pixelAtScreen(16, 30);   // left edge of the box: red outline
    EXPECT_GT(on_edge.r, 200);
    EXPECT_LT(on_edge.g, 40);
    const Rgba inside = target.pixelAtScreen(32, 32);     // box interior: untouched black image
    EXPECT_EQ(inside.r, 0);
    EXPECT_EQ(overlay.drawCalls(), 1u);
}

TEST_F(GlSmokeTest, ResourcesAreReleasedWhenDestroyed) {
    GLuint id = 0;
    {
        Texture2D t;
        t.upload(cv::Mat(4, 4, CV_8UC3, cv::Scalar(1)));
        id = t.id();
        EXPECT_TRUE(glIsTexture(id));
    }
    EXPECT_FALSE(glIsTexture(id));
}

