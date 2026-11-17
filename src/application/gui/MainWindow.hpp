#pragma once

#include "AnalyticsPanel.hpp"
#include "DefectPanel.hpp"
#include "GuiState.hpp"
#include "HistoryPanel.hpp"
#include "ImageComparisonPanel.hpp"
#include "InspectionPanel.hpp"
#include "PerformancePanel.hpp"
#include "ResultsPanel.hpp"

#include <filesystem>

namespace optinspect::gui {

/// Builds the workstation layout every frame: toolbar with navigation, a docking layout
/// (inspection inputs left, main view centre, results right, regions/log bottom) and
/// the pages of the main view (workspace, history, analytics, performance).
class MainWindow {
public:
    MainWindow(const viz::VisualizationConfig& config, const std::filesystem::path& shader_dir,
               PerformanceMetrics& metrics);

    void draw(GuiContext& context);

    ImageComparisonPanel& imagePanel() { return image_panel_; }

private:
    void drawToolbar(GuiContext& context);
    void buildDefaultLayout(unsigned int dockspace_id);

    ImageComparisonPanel image_panel_;
    InspectionPanel inspection_panel_;
    ResultsPanel results_panel_;
    DefectPanel defect_panel_;
    HistoryPanel history_panel_;
    AnalyticsPanel analytics_panel_;
    PerformancePanel performance_panel_;
    bool layout_built_ = false;
};

/// Applies the dark industrial ImGui theme.
void applyIndustrialTheme();

}  // namespace optinspect::gui
