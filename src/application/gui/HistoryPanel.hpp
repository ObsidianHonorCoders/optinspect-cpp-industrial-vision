#pragma once

#include "GuiState.hpp"

namespace optinspect::gui {

/// Inspection history page: database selection, filters, result table and details of the
/// selected stored inspection. All data comes from the Phase 2 repositories through
/// GuiInspectionController (background jobs); nothing here touches SQLite directly.
class HistoryPanel {
public:
    void draw(GuiContext& context);

private:
    std::string last_database_;
    bool first_draw_ = true;
};

}  // namespace optinspect::gui
