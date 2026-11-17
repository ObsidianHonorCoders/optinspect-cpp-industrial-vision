// Manual micro-benchmark of the visualization strategies. It compares the approaches used by
// the application with the naive alternatives on the machine it runs on, so every claim in
// docs/visualization-performance.md can be reproduced. Not part of CTest.
//
//   optinspect_visualization_benchmark [--image <file>] [--iterations N] [--output result.json]
#include "visualization/DefectOverlayRenderer.hpp"
#include "visualization/GLContext.hpp"
#include "visualization/GpuTimer.hpp"
#include "visualization/ImageRenderer.hpp"
#include "visualization/Texture2D.hpp"

#include "third_party/nlohmann/json.hpp"

#include <glad/glad.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>

using namespace optinspect::viz;
using nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

struct Stats {
    double avg = 0, min = 0, max = 0, p95 = 0;
    std::size_t n = 0;
};

Stats summarize(std::vector<double> v) {
    Stats s;
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.n = v.size();
    s.avg = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    s.min = v.front();
    s.max = v.back();
    s.p95 = v[std::min(v.size() - 1, static_cast<std::size_t>(0.95 * static_cast<double>(v.size())))];
    return s;
}

json toJson(const Stats& s) {
    return {{"avg_ms", s.avg}, {"min_ms", s.min}, {"max_ms", s.max}, {"p95_ms", s.p95}, {"samples", s.n}};
}

/// Wall time per iteration including GPU completion (glFinish), after a warm-up.
Stats measure(int iterations, const std::function<void()>& body) {
    for (int i = 0; i < 3; ++i) body();
    glFinish();
    std::vector<double> times;
    for (int i = 0; i < iterations; ++i) {
        const auto t0 = Clock::now();
        body();
        glFinish();
        times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
    }
    return summarize(times);
}

cv::Mat makePcbLikeImage(int w, int h) {
    cv::Mat m(h, w, CV_8UC3, cv::Scalar(40, 90, 40));
    cv::RNG rng(1234);
    for (int i = 0; i < 400; ++i) {
        const cv::Point a(rng.uniform(0, w), rng.uniform(0, h));
        const cv::Point b(rng.uniform(0, w), rng.uniform(0, h));
        cv::line(m, a, b, cv::Scalar(rng.uniform(100, 255), rng.uniform(100, 255), rng.uniform(0, 80)), 3);
    }
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    std::string image_path;
    std::string output = "visualization_benchmark.json";
    int iterations = 100;
    for (int i = 1; i < argc; ++i) {
        const std::string f = argv[i];
        if (f == "--image" && i + 1 < argc) image_path = argv[++i];
        else if (f == "--iterations" && i + 1 < argc) iterations = std::max(10, std::atoi(argv[++i]));
        else if (f == "--output" && i + 1 < argc) output = argv[++i];
        else {
            std::cerr << "usage: optinspect_visualization_benchmark [--image f] [--iterations n] [--output f]\n";
            return 64;
        }
    }

    try {
        GLContextOptions options;
        options.visible = false;
        options.width = 1600;
        options.height = 900;
        options.vsync = false;
        GLContext context = GLContext::create(options);
        const std::filesystem::path shaders(OPTINSPECT_SHADER_SOURCE_DIR);

        json report;
        report["opengl"] = {{"version", context.info().version}, {"renderer", context.info().renderer},
                            {"vendor", context.info().vendor}};
        report["iterations"] = iterations;

        // Source image.
        cv::Mat image;
        if (!image_path.empty()) {
            const auto t0 = Clock::now();
            image = cv::imread(image_path, cv::IMREAD_COLOR);
            report["decode_ms_on_calling_thread"] = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        }
        if (image.empty()) image = makePcbLikeImage(4000, 3000);
        report["image"] = {{"width", image.cols}, {"height", image.rows}, {"channels", image.channels()},
                           {"source", image_path.empty() ? "synthetic" : image_path}};

        // Offscreen target the size of a typical main viewport.
        const int vw = 1200, vh = 800;
        GLuint color = 0, fbo = 0;
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vw, vh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("FBO incomplete");

        VisualizationConfig cfg;
        ViewportTransform transform(cfg);
        transform.setImageSize(image.cols, image.rows);
        transform.setViewport({0, 0}, {static_cast<double>(vw), static_cast<double>(vh)});
        transform.fit();
        const RectI gl_vp = toGlViewportRect({0, 0, static_cast<double>(vw), static_cast<double>(vh)}, 1, 1, vh);

        // 1. Texture upload strategies ---------------------------------------------------------------
        json upload;
        upload["new_texture_every_time_with_mipmaps"] = toJson(measure(iterations, [&] {
            Texture2D t;
            t.upload(image, true);
        }));
        upload["new_texture_every_time_no_mipmaps"] = toJson(measure(iterations, [&] {
            Texture2D t;
            t.upload(image, false);
        }));
        {
            Texture2D reused;
            reused.upload(image, true);
            upload["replace_same_size_with_mipmaps_glTexSubImage2D"] = toJson(measure(iterations, [&] { reused.upload(image, true); }));
            Texture2D reused_nm;
            reused_nm.upload(image, false);
            upload["replace_same_size_no_mipmaps_glTexSubImage2D"] = toJson(measure(iterations, [&] { reused_nm.upload(image, false); }));
            report["texture_bytes_with_mips"] = reused.gpuBytes();
            report["texture_bytes_base_level"] = reused.format().gpuBytes();
        }
        {
            // Non-contiguous sub-matrix: in-place (GL_UNPACK_ROW_LENGTH) vs forced contiguous copy.
            const cv::Mat roi = image(cv::Rect(image.cols / 8, image.rows / 8, image.cols * 3 / 4, image.rows * 3 / 4));
            Texture2D t;
            t.upload(roi, false);
            upload["submatrix_in_place_row_length"] = toJson(measure(iterations, [&] { t.upload(roi, false); }));
            upload["submatrix_with_clone_first"] = toJson(measure(iterations, [&] {
                const cv::Mat copy = roi.clone();
                t.upload(copy, false);
            }));
        }
        upload["unchanged_texture_reused_from_cache"] = {{"avg_ms", 0.0}, {"note", "no GL call is issued"}};
        report["texture_upload_strategies"] = upload;

        // 2. Per-frame rendering ------------------------------------------------------------------------
        json frame;
        ImageRenderer renderer(shaders, cfg);
        Texture2D with_mips;
        with_mips.upload(image, true);
        Texture2D without_mips;
        without_mips.upload(image, false);
        without_mips.setFilter(TextureFilter::Linear);

        const auto drawOnly = [&](Texture2D& t) {
            renderer.beginViewport(gl_vp, cfg.viewport_background);
            renderer.drawImage(t, transform);
            renderer.endViewport();
        };
        frame["fit_to_window_mipmapped_trilinear_cached_texture"] = toJson(measure(iterations, [&] { drawOnly(with_mips); }));
        // Force bilinear on the mipmapped texture's level 0 for the comparison: separate texture without mip chain.
        frame["fit_to_window_no_mipmaps_bilinear_cached_texture"] = toJson(measure(iterations, [&] { drawOnly(without_mips); }));
        {
            Texture2D naive;
            naive.upload(image, false);
            frame["naive_reupload_every_frame_then_draw"] = toJson(measure(iterations, [&] {
                naive.upload(image, false);
                drawOnly(naive);
            }));
        }
        {
            GpuTimer timer;
            std::vector<double> gpu;
            for (int i = 0; i < iterations + 10; ++i) {
                timer.begin();
                drawOnly(with_mips);
                timer.end();
                glFinish();
                if (i >= 10 && timer.lastMs() > 0.0) gpu.push_back(timer.lastMs());
            }
            frame["gpu_time_fit_draw_with_timer_query"] = toJson(summarize(gpu));
        }
        report["frame_rendering"] = frame;

        // 2b. Image quality when minified (fit-to-window): mean absolute error against an area-averaged
        // reference resize of the same image. Lower is better.
        {
            const auto readbackError = [&](Texture2D& t) {
                drawOnly(t);
                glFinish();
                const RectD r = transform.imageScreenRect();
                const int rw = static_cast<int>(r.w), rh = static_cast<int>(r.h);
                const int rx = static_cast<int>(std::ceil(r.x)), ry = static_cast<int>(std::ceil(r.y));
                cv::Mat rgba(rh - 2, rw - 2, CV_8UC4);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(rx + 1, vh - (ry + rh - 1), rw - 2, rh - 2, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data);
                cv::flip(rgba, rgba, 0);
                cv::Mat bgr;
                cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
                cv::Mat reference;
                cv::resize(image, reference, cv::Size(rw, rh), 0, 0, cv::INTER_AREA);
                const cv::Mat crop = reference(cv::Rect(1, 1, rw - 2, rh - 2));
                return cv::mean(cv::abs(crop - bgr))[0];
            };
            json quality;
            quality["fit_zoom"] = transform.zoom();
            quality["mean_abs_error_vs_area_resize_mipmapped"] = readbackError(with_mips);
            quality["mean_abs_error_vs_area_resize_bilinear_no_mipmaps"] = readbackError(without_mips);
            report["minification_quality"] = quality;
        }

        // 3. Overlay batching -----------------------------------------------------------------------------
        json overlay;
        DefectOverlayRenderer overlay_renderer(shaders);
        for (int count : {10, 100, 500}) {
            std::vector<DefectBox> boxes;
            for (int i = 0; i < count; ++i) {
                boxes.push_back({{static_cast<double>((i * 37) % (image.cols - 60)), static_cast<double>((i * 53) % (image.rows - 60)), 40.0, 30.0}, i + 1, 1200.0});
            }
            const Vec2 size = transform.viewportSize();
            overlay["batched_single_draw_" + std::to_string(count)] = toJson(measure(iterations, [&] {
                renderer.beginViewport(gl_vp, cfg.viewport_background);
                const OverlayGeometry g = buildDefectOverlay(boxes, std::nullopt, transform, cfg.overlay);
                overlay_renderer.render(g, size);
                renderer.endViewport();
            }));
            overlay["one_draw_per_box_" + std::to_string(count)] = toJson(measure(iterations, [&] {
                renderer.beginViewport(gl_vp, cfg.viewport_background);
                for (const DefectBox& b : boxes) {
                    const OverlayGeometry g = buildDefectOverlay({b}, std::nullopt, transform, cfg.overlay);
                    overlay_renderer.render(g, size);
                }
                renderer.endViewport();
            }));
        }
        report["overlay_rendering"] = overlay;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &color);

        std::ofstream out(output);
        out << report.dump(2) << "\n";
        std::cout << report.dump(2) << "\nWritten: " << output << "\n";
    } catch (const GLContextError& e) {
        std::cerr << "No OpenGL context: " << e.what() << "\n";
        return 77;
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
