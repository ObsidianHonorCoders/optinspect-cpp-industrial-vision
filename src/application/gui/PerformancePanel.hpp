#pragma once

#include "GuiState.hpp"

namespace optinspect::gui {

/// Performance and diagnostics page: OpenGL driver info, startup figures, frame-time
/// breakdown (CPU / UI / GPU kept separate), texture memory, cache behaviour and
/// worker / database timings. Everything shown is measured at run time.
class PerformancePanel {
public:
    void draw(GuiContext& context);

private:
    void drawContent(GuiContext& context);
};

}  // namespace optinspect::gui
