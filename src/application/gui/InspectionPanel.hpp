#pragma once

#include "GuiState.hpp"

namespace optinspect::gui {

/// Left panel: inspection inputs (images, configuration, output, database, product,
/// serial), the Run button and the live execution status.
class InspectionPanel {
public:
    void draw(GuiContext& context);

private:
    bool pathRow(GuiContext& context, const char* label, std::string& value, bool is_folder,
                 const char* dialog_title);
};

}  // namespace optinspect::gui
