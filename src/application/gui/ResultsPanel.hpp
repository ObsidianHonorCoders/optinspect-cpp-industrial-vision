#pragma once

#include "GuiState.hpp"

namespace optinspect::gui {

/// Right panel: decision, identifiers, alignment, region count, timings and a compact region list.
class ResultsPanel {
public:
    void draw(GuiContext& context);
};

/// Colour used for a decision text ("PASS", "FAIL", "REVIEW").
unsigned int decisionColor(const std::string& decision);

}  // namespace optinspect::gui
