#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace optinspect::gui {

/// Command line of optinspect_gui. Every option only pre-fills what the user could do
/// by hand in the window; the scripted ones (--run, --screenshot, --benchmark) exist
/// for reproducible documentation screenshots and measurements.
struct GuiOptions {
    std::string reference_path;
    std::string inspected_path;
    std::string config_path;
    std::string output_dir;
    std::string database_path;
    std::optional<std::int64_t> product_id;
    std::string serial_number;

    bool run = false;                       ///< Start an inspection as soon as the window is up.
    std::optional<std::int64_t> load_inspection;  ///< Load this stored inspection (needs --database).
    std::string view_mode;                  ///< reference|inspected|annotated|difference|side-by-side|blend
    std::string page;                       ///< workspace|history|analytics|performance
    std::optional<int> select_defect;       ///< 1-based region index to highlight.
    std::string roi;                        ///< "x,y,w,h" in image pixels.
    bool simulate_input = false;            ///< Replay a scripted wheel-zoom, drag-pan and ROI drag through the real input path.

    std::string screenshot_path;            ///< Save the framebuffer to this PNG, then exit.
    int screenshot_frames = 12;             ///< Frames to render after the view is ready, before capturing.
    bool keep_open = false;                 ///< With --screenshot: do not exit afterwards.

    std::string benchmark_path;             ///< Run the scripted benchmark, write JSON here, exit.
    int benchmark_frames = 300;             ///< Frames per measured scenario.

    std::string shader_dir;                 ///< Override of the shader directory.
    int width = 1600;
    int height = 950;
    bool hidden = false;
    bool no_vsync = false;
    bool show_help = false;
};

/// Throws std::invalid_argument with a descriptive message for bad input.
GuiOptions parseGuiOptions(int argc, const char* const* argv);

std::string guiUsageText();

/// Parses "x,y,w,h"; returns nullopt when malformed or non-positive size.
struct RoiSpec {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};
std::optional<RoiSpec> parseRoiSpec(const std::string& text);

}  // namespace optinspect::gui
