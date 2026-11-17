#include "GuiOptions.hpp"

#include <sstream>
#include <stdexcept>

namespace optinspect::gui {

namespace {

long long toInt(const std::string& text, const std::string& flag) {
    try {
        std::size_t used = 0;
        const long long v = std::stoll(text, &used);
        if (used != text.size()) throw std::invalid_argument("");
        return v;
    } catch (const std::exception&) {
        throw std::invalid_argument(flag + " expects a whole number, got '" + text + "'");
    }
}

}  // namespace

std::optional<RoiSpec> parseRoiSpec(const std::string& text) {
    RoiSpec r;
    char c1 = 0, c2 = 0, c3 = 0;
    std::istringstream in(text);
    if (!(in >> r.x >> c1 >> r.y >> c2 >> r.w >> c3 >> r.h) || c1 != ',' || c2 != ',' || c3 != ',') return std::nullopt;
    in >> std::ws;
    if (!in.eof() || r.w <= 0 || r.h <= 0 || r.x < 0 || r.y < 0) return std::nullopt;
    return r;
}

GuiOptions parseGuiOptions(int argc, const char* const* argv) {
    GuiOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + flag);
            return argv[++i];
        };
        if (flag == "--help" || flag == "-h") o.show_help = true;
        else if (flag == "--reference") o.reference_path = value();
        else if (flag == "--input") o.inspected_path = value();
        else if (flag == "--config") o.config_path = value();
        else if (flag == "--output") o.output_dir = value();
        else if (flag == "--database") o.database_path = value();
        else if (flag == "--product-id") o.product_id = toInt(value(), flag);
        else if (flag == "--serial") o.serial_number = value();
        else if (flag == "--run") o.run = true;
        else if (flag == "--load-inspection") o.load_inspection = toInt(value(), flag);
        else if (flag == "--view") o.view_mode = value();
        else if (flag == "--page") o.page = value();
        else if (flag == "--select-defect") o.select_defect = static_cast<int>(toInt(value(), flag));
        else if (flag == "--roi") {
            o.roi = value();
            if (!parseRoiSpec(o.roi)) throw std::invalid_argument("--roi expects x,y,w,h with a positive size");
        }
        else if (flag == "--simulate-input") o.simulate_input = true;
        else if (flag == "--screenshot") o.screenshot_path = value();
        else if (flag == "--screenshot-frames") o.screenshot_frames = static_cast<int>(toInt(value(), flag));
        else if (flag == "--keep-open") o.keep_open = true;
        else if (flag == "--benchmark") o.benchmark_path = value();
        else if (flag == "--frames") o.benchmark_frames = static_cast<int>(toInt(value(), flag));
        else if (flag == "--shader-dir") o.shader_dir = value();
        else if (flag == "--width") o.width = static_cast<int>(toInt(value(), flag));
        else if (flag == "--height") o.height = static_cast<int>(toInt(value(), flag));
        else if (flag == "--hidden") o.hidden = true;
        else if (flag == "--no-vsync") o.no_vsync = true;
        else throw std::invalid_argument("Unknown option: " + flag);
    }
    if (o.width < 640 || o.height < 480) throw std::invalid_argument("--width/--height must be at least 640x480");
    if (o.benchmark_frames < 10) throw std::invalid_argument("--frames must be at least 10");
    if (o.load_inspection && o.database_path.empty()) throw std::invalid_argument("--load-inspection needs --database");
    return o;
}

std::string guiUsageText() {
    return R"(OptiInspect GUI - interactive inspection workstation

Usage: optinspect_gui [options]

Inspection inputs (pre-fill the left panel):
  --reference <file>       Reference image
  --input <file>           Inspected image
  --config <file>          Configuration (default config/inspection_config.json)
  --output <dir>           Output directory (default results)
  --database <file>        SQLite database; also enables "store result"
  --product-id <n>         Product id stored with the inspection
  --serial <text>          Serial number stored with the inspection

Scripted use (documentation screenshots, measurements):
  --run                    Run the inspection as soon as the window is up
  --load-inspection <id>   Load a stored inspection (needs --database)
  --view <mode>            reference | inspected | annotated | difference | side-by-side | blend
  --page <page>            workspace | history | analytics | performance
  --select-defect <n>      Highlight region n (1-based)
  --roi x,y,w,h            Preset a region of interest (image pixels)
  --simulate-input         Replay a wheel zoom, a drag pan and an ROI drag with synthetic mouse events
  --screenshot <png>       Save the window contents after the view is ready, then exit
  --screenshot-frames <n>  Frames to render before the capture (default 12)
  --keep-open              Do not exit after the screenshot
  --benchmark <json>       Run the measurement scenarios, write the results, exit
  --frames <n>             Frames per benchmark scenario (default 300)

Window:
  --width <px> --height <px>   Initial window size (default 1600x950)
  --hidden                     Create the window hidden
  --no-vsync                   Disable vertical sync
  --shader-dir <dir>           Directory with image.vert/frag and overlay.vert/frag
  -h, --help                   Show this help
)";
}

}  // namespace optinspect::gui
