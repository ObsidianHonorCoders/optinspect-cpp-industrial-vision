#include "GuiApplication.hpp"

#include "ImageLoader.hpp"
#include "NativeFileDialog.hpp"

#include "common/Timing.hpp"
#include "third_party/nlohmann/json.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <glad/glad.h>

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

namespace optinspect::gui {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

/// Shader directory: explicit option, next to the executable (copied there by the build),
/// then the source tree location baked in at configure time.
fs::path findShaderDirectory(const std::string& override_dir) {
    std::vector<fs::path> candidates;
    if (!override_dir.empty()) candidates.emplace_back(override_dir);
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (n > 0) candidates.push_back(fs::path(buffer).parent_path() / "resources" / "shaders");
#endif
    candidates.emplace_back(fs::path("resources") / "shaders");
#ifdef OPTINSPECT_SHADER_SOURCE_DIR
    candidates.emplace_back(OPTINSPECT_SHADER_SOURCE_DIR);
#endif
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c / "image.vert", ec)) return c;
    }
    throw std::runtime_error("Shader directory not found (looked for resources/shaders/image.vert). "
                             "Use --shader-dir <dir>.");
}

viz::ViewMode parseViewMode(const std::string& text, viz::ViewMode fallback) {
    if (text == "reference") return viz::ViewMode::Reference;
    if (text == "inspected") return viz::ViewMode::Inspected;
    if (text == "annotated") return viz::ViewMode::Annotated;
    if (text == "difference") return viz::ViewMode::Difference;
    if (text == "side-by-side") return viz::ViewMode::SideBySide;
    if (text == "blend") return viz::ViewMode::Blend;
    return fallback;
}

Page parsePage(const std::string& text, Page fallback) {
    if (text == "workspace") return Page::Workspace;
    if (text == "history") return Page::History;
    if (text == "analytics") return Page::Analytics;
    if (text == "performance") return Page::Performance;
    return fallback;
}

struct Series {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double avg() const { return v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size()); }
    double max() const { return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end()); }
    double min() const { return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end()); }
    double percentile(double p) const {
        if (v.empty()) return 0.0;
        std::vector<double> s = v;
        std::sort(s.begin(), s.end());
        return s[std::min(s.size() - 1, static_cast<std::size_t>(p * static_cast<double>(s.size())))];
    }
    nlohmann::json json() const {
        return {{"samples", v.size()}, {"avg_ms", avg()}, {"p95_ms", percentile(0.95)}, {"max_ms", max()}, {"min_ms", min()}};
    }
};

}  // namespace

GuiApplication::GuiApplication(GuiOptions options, Clock::time_point process_start)
    : options_(std::move(options)), process_start_(process_start) {
    viz_config_.vsync = !(options_.no_vsync || !options_.benchmark_path.empty());
    const fs::path shader_dir = findShaderDirectory(options_.shader_dir);

    viz::GLContextOptions gl_options;
    gl_options.title = "OptiInspect - Industrial Optical Inspection";
    gl_options.width = options_.width;
    gl_options.height = options_.height;
    gl_options.visible = !options_.hidden;
    gl_options.vsync = viz_config_.vsync;
    gl_options.maximized = false;
    gl_ = std::make_unique<viz::GLContext>(viz::GLContext::create(gl_options));
    metrics_.gl_init_ms = gl_->creationTimeMs();

    const Clock::time_point renderer_start = Clock::now();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imgui_ready_ = true;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // the default layout is rebuilt on every start
    const char* font_candidates[] = {"C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf"};
    for (const char* f : font_candidates) {
        std::error_code ec;
        if (fs::is_regular_file(f, ec) && io.Fonts->AddFontFromFileTTF(f, 16.0f)) break;
    }
    applyIndustrialTheme();
    ImGui_ImplGlfw_InitForOpenGL(gl_->window(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    controller_ = std::make_unique<GuiInspectionController>(
        [this]() {
            return main_window_ ? main_window_->imagePanel().cachedKeys() : std::unordered_set<std::string>{};
        },
        &metrics_);
    main_window_ = std::make_unique<MainWindow>(viz_config_, shader_dir, metrics_);
    metrics_.renderer_init_ms = msSince(renderer_start);

    applyOptionsToState();
    last_frame_start_ = Clock::now();
}

GuiApplication::~GuiApplication() {
    // GPU resources first, while the GL context is still current.
    if (gl_) gl_->makeCurrent();
    main_window_.reset();
    controller_.reset();
    if (imgui_ready_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    gl_.reset();
}

void GuiApplication::applyOptionsToState() {
    InspectionSettings& s = controller_->settings();
    if (!options_.reference_path.empty()) s.reference_path = options_.reference_path;
    if (!options_.inspected_path.empty()) s.inspected_path = options_.inspected_path;
    if (!options_.config_path.empty()) s.config_path = options_.config_path;
    if (!options_.output_dir.empty()) s.output_dir = options_.output_dir;
    if (!options_.database_path.empty()) {
        s.database_path = options_.database_path;
        s.persist = true;
    }
    if (options_.product_id) s.product_id_text = std::to_string(*options_.product_id);
    s.serial_number = options_.serial_number;

    view_state_.mode = parseViewMode(options_.view_mode, view_state_.mode);
    view_state_.page = parsePage(options_.page, view_state_.page);
    if (options_.select_defect) view_state_.selected_defect = options_.select_defect;
    wants_screenshot_ = false;
}

bool GuiApplication::captureScreenshot(const std::string& path) {
    int w = 0, h = 0;
    gl_->framebufferSize(w, h);
    if (w <= 0 || h <= 0) return false;
    cv::Mat image(h, w, CV_8UC3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, image.data);
    cv::flip(image, image, 0);
    std::error_code ec;
    if (fs::path(path).has_parent_path()) fs::create_directories(fs::path(path).parent_path(), ec);
    return cv::imwrite(path, image);
}

void GuiApplication::renderFrame() {
    const Clock::time_point frame_start = Clock::now();
    const double since_last = std::chrono::duration<double, std::milli>(frame_start - last_frame_start_).count();
    last_frame_start_ = frame_start;
    if (first_frame_presented_) {
        metrics_.frame_ms.add(since_last);
        if (controller_->inspectionRunning()) metrics_.frame_ms_during_inspection.add(since_last);
    }

    viz::pollEvents();
    controller_->update();
    ImageComparisonPanel& images = main_window_->imagePanel();
    images.syncTextures(controller_->view());

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const Clock::time_point ui_start = Clock::now();
    GuiContext context{*controller_, view_state_, metrics_, gl_->info(), nullptr};
#if defined(_WIN32)
    context.native_window = glfwGetWin32Window(gl_->window());
#endif
    main_window_->draw(context);
    metrics_.ui_build_ms.add(msSince(ui_start));

    ImGui::Render();

    int fb_w = 0, fb_h = 0, win_w = 1, win_h = 1;
    gl_->framebufferSize(fb_w, fb_h);
    gl_->windowSize(win_w, win_h);
    glViewport(0, 0, fb_w, fb_h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.075f, 0.085f, 0.100f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    images.renderGl(fb_h, win_w > 0 ? static_cast<double>(fb_w) / win_w : 1.0,
                    win_h > 0 ? static_cast<double>(fb_h) / win_h : 1.0);

    const Clock::time_point imgui_start = Clock::now();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    metrics_.imgui_render_ms.add(msSince(imgui_start));

    if (wants_screenshot_) {
        wants_screenshot_ = false;
        if (captureScreenshot(pending_screenshot_)) {
            std::cout << "Screenshot written: " << pending_screenshot_ << "\n";
        } else {
            std::cerr << "Could not write the screenshot: " << pending_screenshot_ << "\n";
            exit_code_ = 3;
        }
        if (!options_.keep_open) gl_->requestClose();
    }

    metrics_.cpu_frame_ms.add(msSince(frame_start));
    gl_->swapBuffers();

    if (!first_frame_presented_) {
        first_frame_presented_ = true;
        metrics_.startup_ms = std::chrono::duration<double, std::milli>(Clock::now() - process_start_).count();
    }
}

/// Replays a wheel zoom, a left-drag pan and an ROI drag through the real input path
/// (GLFW cursor + ImGui input queue), so scripted runs exercise the same code as a user.
void GuiApplication::simulateInput(int f) {
    ImGuiIO& io = ImGui::GetIO();
    const viz::Vec2 c = main_window_->imagePanel().mainViewportCenter();
    float x = static_cast<float>(c.x);
    float y = static_cast<float>(c.y);
    const auto moveTo = [&](float px, float py) {
        io.AddMousePosEvent(px, py);
        glfwSetCursorPos(gl_->window(), px, py);
    };
    switch (f) {
        case 0: moveTo(x, y); break;
        case 1: io.AddMouseWheelEvent(0.0f, 4.0f); break;                                  // zoom in around the cursor
        case 3: moveTo(x, y); io.AddMouseButtonEvent(0, true); break;                     // start a pan drag
        case 4: moveTo(x + 40, y + 25); break;
        case 5: moveTo(x + 90, y + 55); break;
        case 6: io.AddMouseButtonEvent(0, false); break;
        case 8: view_state_.tool = viz::ViewportTool::Roi; moveTo(x - 140, y - 90); break;
        case 10: moveTo(x - 140, y - 90); io.AddMouseButtonEvent(0, true); break;         // ROI drag
        case 11: moveTo(x - 60, y - 40); break;
        case 12: moveTo(x + 60, y + 50); break;
        case 14: io.AddMouseButtonEvent(0, false); break;
        default: break;
    }
}

bool GuiApplication::stepScript() {
    ++script_frames_;
    switch (script_stage_) {
        case ScriptStage::Start: {
            bool started = true;
            if (options_.run) started = controller_->runInspection();
            else if (options_.load_inspection) started = controller_->selectInspection(*options_.load_inspection);
            if (!started) {
                std::cerr << "Scripted start failed: "
                          << (controller_->lastInspectionError().empty() ? "see log" : controller_->lastInspectionError())
                          << "\n";
                exit_code_ = 2;
                return true;
            }
            script_stage_ = ScriptStage::WaitingForResult;
            return false;
        }
        case ScriptStage::WaitingForResult: {
            controller_->update();  // install a result that finished since the last frame
            if (controller_->anyBusy()) return false;
            if (options_.run || options_.load_inspection) {
                if (!controller_->view().hasResult()) {
                    std::cerr << "Scripted run produced no result: "
                              << (controller_->lastInspectionError().empty() ? "see log" : controller_->lastInspectionError())
                              << "\n";
                    exit_code_ = 2;
                    return true;
                }
            }
            if (options_.select_defect) view_state_.request_center_selected = false;
            script_stage_ = ScriptStage::Settling;
            settle_frames_ = 0;
            return false;
        }
        case ScriptStage::Settling: {
            if (settle_frames_ == 1) {
                if (const auto roi = parseRoiSpec(options_.roi)) {
                    main_window_->imagePanel().presetRoi({roi->x, roi->y, roi->w, roi->h});
                }
            }
            if (options_.simulate_input) simulateInput(settle_frames_);
            const int needed = std::max(options_.screenshot_frames, options_.simulate_input ? 26 : 0);
            if (++settle_frames_ >= needed) {
                pending_screenshot_ = options_.screenshot_path;
                wants_screenshot_ = true;
                script_stage_ = ScriptStage::Done;
            }
            return false;
        }
        case ScriptStage::Done: return false;
    }
    return false;
}

int GuiApplication::run() {
    std::cout << "OpenGL " << gl_->info().version << " | " << gl_->info().renderer << " | " << gl_->info().vendor << "\n";

    if (!options_.benchmark_path.empty()) return runBenchmark();

    const bool scripted = !options_.screenshot_path.empty();
    const Clock::time_point script_start = Clock::now();
    // Without --screenshot, --run / --load-inspection still start their job once.
    ScriptStage stage_after = ScriptStage::Start;
    (void)stage_after;
    bool started_unscripted = false;

    while (!gl_->shouldClose()) {
        if (scripted) {
            if (stepScript()) break;
            if (msSince(script_start) > 180000.0) {
                std::cerr << "Scripted run timed out.\n";
                exit_code_ = 2;
                break;
            }
        } else if (!started_unscripted) {
            started_unscripted = true;
            if (options_.run) controller_->runInspection();
            if (options_.load_inspection) controller_->selectInspection(*options_.load_inspection);
        }
        renderFrame();
    }
    return exit_code_;
}

int GuiApplication::runBenchmark() {
    using nlohmann::json;
    ImageComparisonPanel& images = main_window_->imagePanel();
    images.setSynchronousUploadTiming(true);
    gl_->setSwapInterval(0);

    json report;
    report["vsync"] = false;
    report["opengl"] = {{"version", gl_->info().version},
                        {"renderer", gl_->info().renderer},
                        {"vendor", gl_->info().vendor},
                        {"glsl", gl_->info().glsl_version},
                        {"max_texture_size", gl_->info().max_texture_size}};
    int fb_w = 0, fb_h = 0;
    gl_->framebufferSize(fb_w, fb_h);
    report["window_framebuffer"] = {{"width", fb_w}, {"height", fb_h}};
    report["frames_per_scenario"] = options_.benchmark_frames;

    const auto sampleFrames = [&](int frames, const std::function<void(int)>& per_frame) {
        struct Out { Series frame, cpu, ui, submit, gpu, imgui; } out;
        for (int i = 0; i < frames; ++i) {
            if (per_frame) per_frame(i);
            renderFrame();
            if (i > 5) {  // skip warm-up
                out.frame.add(metrics_.frame_ms.last());
                out.cpu.add(metrics_.cpu_frame_ms.last());
                out.ui.add(metrics_.ui_build_ms.last());
                out.submit.add(metrics_.gl_submit_ms.last());
                out.imgui.add(metrics_.imgui_render_ms.last());
                if (!metrics_.gpu_render_ms.empty()) out.gpu.add(metrics_.gpu_render_ms.last());
            }
        }
        return json{{"frame_total", out.frame.json()}, {"cpu_frame_no_swap", out.cpu.json()},
                    {"ui_build", out.ui.json()},       {"gl_submit_cpu", out.submit.json()},
                    {"gpu_image_overlay", out.gpu.json()}, {"imgui_render_cpu", out.imgui.json()}};
    };

    // Startup figures (the first frame has to be presented before the end-to-end startup time is known).
    renderFrame();
    report["startup"] = {{"to_first_frame_ms", metrics_.startup_ms},
                         {"gl_context_init_ms", metrics_.gl_init_ms},
                         {"renderer_and_imgui_init_ms", metrics_.renderer_init_ms}};
    for (int i = 0; i < 20; ++i) renderFrame();
    report["scenario_empty_ui"] = sampleFrames(options_.benchmark_frames, nullptr);

    // Image decode on the calling thread (what the worker does for display images).
    const std::string ref = controller_->settings().reference_path;
    const std::string insp = controller_->settings().inspected_path;
    if (!ref.empty() && !insp.empty()) {
        Series decode_ref, decode_insp;
        for (int i = 0; i < 5; ++i) {
            decode_ref.add(loadImageSlot(ref, {}).load_ms);
            decode_insp.add(loadImageSlot(insp, {}).load_ms);
        }
        const cv::Mat m = cv::imread(ref, cv::IMREAD_UNCHANGED);
        report["images"] = {{"reference_path", ref},
                            {"width", m.cols},
                            {"height", m.rows},
                            {"channels", m.channels()},
                            {"decode_reference_ms", decode_ref.json()},
                            {"decode_inspected_ms", decode_insp.json()}};

        // Inspection on the worker thread while the UI keeps rendering.
        const std::size_t uploads_before = metrics_.texture_upload_ms.totalCount();
        const std::size_t skipped_before = metrics_.images_skipped_cached;
        metrics_.frame_ms_during_inspection.clear();
        if (!controller_->runInspection()) {
            std::cerr << "Benchmark: inspection could not start: " << controller_->lastInspectionError() << "\n";
            return 2;
        }
        Series during;
        const Clock::time_point t0 = Clock::now();
        while (controller_->anyBusy() && msSince(t0) < 120000.0) {
            renderFrame();
            during.add(metrics_.frame_ms.last());
        }
        for (int i = 0; i < 5; ++i) renderFrame();  // install result + upload textures
        report["inspection_first_run"] = {{"ui_frame_ms_while_worker_runs", during.json()},
                                          {"worker_ms", metrics_.inspection_worker_ms.last()},
                                          {"wall_ms", msSince(t0)},
                                          {"texture_uploads", metrics_.texture_upload_ms.totalCount() - uploads_before},
                                          {"decodes_skipped_cached", metrics_.images_skipped_cached - skipped_before},
                                          {"decision", controller_->view().decision},
                                          {"regions", controller_->view().defects.size()}};
        json uploads = json::array();
        for (double ms : metrics_.texture_upload_ms.samples()) uploads.push_back(ms);
        report["texture_upload_ms_each_image_incl_gpu_sync"] = uploads;
        report["texture_memory_bytes"] = metrics_.texture_bytes;
        report["texture_count"] = metrics_.texture_count;

        // View scenarios.
        const struct { viz::ViewMode mode; const char* name; bool animate; } scenarios[] = {
            {viz::ViewMode::Annotated, "annotated_zoom_pan_animation", true},
            {viz::ViewMode::Annotated, "annotated_static", false},
            {viz::ViewMode::SideBySide, "side_by_side_static", false},
            {viz::ViewMode::Blend, "blend_zoom_pan_animation", true}};
        json view_results = json::object();
        for (const auto& sc : scenarios) {
            view_state_.mode = sc.mode;
            view_state_.page = Page::Workspace;
            view_state_.request_fit = true;
            for (int i = 0; i < 10; ++i) renderFrame();
            view_results[sc.name] = sampleFrames(options_.benchmark_frames, [&](int i) {
                if (!sc.animate) return;
                viz::ViewportTransform& t = images.mainViewport().transform();
                const viz::Vec2 c{t.viewportOrigin().x + t.viewportSize().x * 0.5, t.viewportOrigin().y + t.viewportSize().y * 0.5};
                t.zoomAt(c, (i / 60) % 2 == 0 ? 1.03 : 1.0 / 1.03);
                t.panBy({(i % 40 < 20) ? 3.0 : -3.0, 0.0});
            });
        }
        report["view_scenarios"] = view_results;

        // Second run of the same inputs: reference/inspected textures come from the cache.
        view_state_.mode = viz::ViewMode::Annotated;
        const std::size_t uploads2 = metrics_.texture_upload_ms.totalCount();
        const std::size_t skipped2 = metrics_.images_skipped_cached;
        const Clock::time_point t1 = Clock::now();
        controller_->runInspection();
        while (controller_->anyBusy() && msSince(t1) < 120000.0) renderFrame();
        for (int i = 0; i < 5; ++i) renderFrame();
        report["inspection_second_run_same_inputs"] = {{"worker_ms", metrics_.inspection_worker_ms.last()},
                                                       {"wall_ms", msSince(t1)},
                                                       {"texture_uploads", metrics_.texture_upload_ms.totalCount() - uploads2},
                                                       {"decodes_skipped_cached", metrics_.images_skipped_cached - skipped2},
                                                       {"texture_cache_hits", metrics_.cache_hits},
                                                       {"texture_cache_misses", metrics_.cache_misses}};
    } else {
        report["images"] = "no --reference/--input given: image scenarios skipped";
    }

    // Database queries.
    const std::string db = controller_->settings().database_path;
    std::error_code ec;
    if (controller_->settings().persist && fs::is_regular_file(db, ec)) {
        Series history, details;
        for (int i = 0; i < 10; ++i) {
            controller_->refreshHistory();
            while (controller_->anyBusy()) renderFrame();
            renderFrame();
            history.add(metrics_.db_query_ms.last());
        }
        if (controller_->history() && !controller_->history()->inspections.empty()) {
            const std::int64_t id = controller_->history()->inspections.front().id;
            for (int i = 0; i < 5; ++i) {
                controller_->selectInspection(id);
                while (controller_->anyBusy()) renderFrame();
                renderFrame();
                details.add(metrics_.db_query_ms.last());
            }
        }
        report["database"] = {{"history_query_ms", history.json()},
                              {"inspection_with_images_load_ms", details.json()},
                              {"rows_returned", controller_->history() ? controller_->history()->inspections.size() : 0}};
    }

    std::ofstream out(options_.benchmark_path);
    if (!out) {
        std::cerr << "Cannot write " << options_.benchmark_path << "\n";
        return 3;
    }
    out << report.dump(2) << "\n";
    std::cout << "Benchmark written: " << options_.benchmark_path << "\n";
    return 0;
}

}  // namespace optinspect::gui
