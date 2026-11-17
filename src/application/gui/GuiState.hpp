#pragma once

#include "GuiInspectionController.hpp"
#include "GuiMetrics.hpp"

#include "visualization/GLContext.hpp"
#include "visualization/ImageViewport.hpp"
#include "visualization/VisualizationTypes.hpp"

#include <optional>
#include <string>

namespace optinspect::gui {

enum class Page { Workspace, History, Analytics, Performance };

/// UI state shared by the toolbar, the viewport and the tables.
struct ViewState {
    Page page = Page::Workspace;
    viz::ViewMode mode = viz::ViewMode::Annotated;
    viz::ViewportTool tool = viz::ViewportTool::Pan;

    bool show_overlay = true;
    bool show_labels = true;
    bool show_area = false;
    float blend = 0.5f;  ///< Weight of the inspected image in Blend mode.

    std::optional<int> selected_defect;  ///< DefectRow::index of the highlighted region.
    bool center_on_select = true;

    // One-shot requests consumed by the image panel.
    bool request_fit = false;
    bool request_actual = false;
    bool request_center_selected = false;
    bool request_clear_roi = false;
    bool request_export_roi = false;
};

/// Everything a panel may read or change. Plain references; the application owns the objects.
struct GuiContext {
    GuiInspectionController& controller;
    ViewState& view;
    PerformanceMetrics& metrics;
    const viz::GLInfo& gl_info;
    void* native_window = nullptr;  ///< HWND on Windows, for the file dialogs.
};

}  // namespace optinspect::gui
