#pragma once

#include "GuiState.hpp"

namespace optinspect::gui {

/// Bottom panel: table of measured regions (actual Phase 1 / stored fields) and the event log.
class DefectPanel {
public:
    void draw(GuiContext& context);

private:
    void drawTable(GuiContext& context);
    void drawLog(GuiContext& context);
    std::size_t last_log_size_ = 0;
};

}  // namespace optinspect::gui
