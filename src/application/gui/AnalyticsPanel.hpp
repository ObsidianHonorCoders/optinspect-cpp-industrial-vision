#pragma once

#include "GuiState.hpp"

namespace optinspect::gui {

/// Quality analytics page. Shows the metrics computed by QualityAnalyticsService over the
/// stored inspections, with simple charts drawn through ImDrawList. The numbers describe
/// detector output only: they are not defect-detection accuracy figures.
class AnalyticsPanel {
public:
    void draw(GuiContext& context);

private:
    std::string last_database_;
    bool first_draw_ = true;
};

}  // namespace optinspect::gui
