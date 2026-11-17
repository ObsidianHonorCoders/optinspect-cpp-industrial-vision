#pragma once

#include "GuiMetrics.hpp"
#include "GuiOptions.hpp"
#include "GuiState.hpp"
#include "MainWindow.hpp"

#include "visualization/GLContext.hpp"
#include "visualization/VisualizationConfig.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace optinspect::gui {

/// Owns the window, the OpenGL context, Dear ImGui and the whole GUI object graph, and
/// runs the main loop (interactive, scripted screenshot, or benchmark).
///
/// Lifetime / destruction order (GPU resources are destroyed while the context is current):
///   main window (textures, renderers) -> controller (joins worker threads)
///   -> ImGui backends -> ImGui context -> GL context / GLFW.
class GuiApplication {
public:
    GuiApplication(GuiOptions options, std::chrono::steady_clock::time_point process_start);
    ~GuiApplication();
    GuiApplication(const GuiApplication&) = delete;
    GuiApplication& operator=(const GuiApplication&) = delete;

    /// Runs until the window is closed / the script is finished. Returns the process exit code.
    int run();

private:
    enum class ScriptStage { Start, WaitingForResult, Settling, Done };

    void renderFrame();
    bool stepScript();
    void simulateInput(int frame);
    void applyOptionsToState();
    bool captureScreenshot(const std::string& path);
    int runBenchmark();

    GuiOptions options_;
    std::chrono::steady_clock::time_point process_start_;
    std::chrono::steady_clock::time_point last_frame_start_;
    bool first_frame_presented_ = false;

    PerformanceMetrics metrics_;
    viz::VisualizationConfig viz_config_;
    std::unique_ptr<viz::GLContext> gl_;
    bool imgui_ready_ = false;

    ViewState view_state_;
    std::unique_ptr<GuiInspectionController> controller_;
    std::unique_ptr<MainWindow> main_window_;

    // Scripted run.
    ScriptStage script_stage_ = ScriptStage::Start;
    int settle_frames_ = 0;
    int script_frames_ = 0;
    int exit_code_ = 0;
    bool wants_screenshot_ = false;
    std::string pending_screenshot_;
};

}  // namespace optinspect::gui
