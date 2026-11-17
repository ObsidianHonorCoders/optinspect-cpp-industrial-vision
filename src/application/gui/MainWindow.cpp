#include "MainWindow.hpp"

#include "NativeFileDialog.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>

namespace optinspect::gui {

namespace {

const std::vector<FileFilter>& imageFilters() {
    static const std::vector<FileFilter> filters = {{"Images", "*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff"},
                                                    {"All files", "*.*"}};
    return filters;
}

bool navButton(const char* label, bool active) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    const bool clicked = ImGui::Button(label);
    if (active) ImGui::PopStyleColor();
    return clicked;
}

}  // namespace

void applyIndustrialTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 3.0f;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 3.0f;
    s.ScrollbarRounding = 3.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(10.0f, 8.0f);
    s.FramePadding = ImVec2(8.0f, 4.0f);
    s.ItemSpacing = ImVec2(8.0f, 6.0f);
    s.CellPadding = ImVec2(6.0f, 3.0f);

    ImVec4* c = s.Colors;
    const ImVec4 bg(0.075f, 0.085f, 0.100f, 1.00f);
    const ImVec4 panel(0.105f, 0.120f, 0.140f, 1.00f);
    const ImVec4 raised(0.160f, 0.180f, 0.210f, 1.00f);
    const ImVec4 accent(0.14f, 0.52f, 0.62f, 1.00f);  // teal
    const ImVec4 accent_hot(0.18f, 0.64f, 0.76f, 1.00f);
    c[ImGuiCol_Text] = ImVec4(0.88f, 0.90f, 0.92f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.57f, 0.62f, 1.00f);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = panel;
    c[ImGuiCol_Border] = ImVec4(0.20f, 0.23f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBg] = raised;
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.23f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.28f, 0.33f, 1.00f);
    c[ImGuiCol_TitleBg] = panel;
    c[ImGuiCol_TitleBgActive] = panel;
    c[ImGuiCol_MenuBarBg] = panel;
    c[ImGuiCol_Button] = raised;
    c[ImGuiCol_ButtonHovered] = accent;
    c[ImGuiCol_ButtonActive] = accent_hot;
    c[ImGuiCol_Header] = ImVec4(0.14f, 0.40f, 0.48f, 0.60f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.14f, 0.52f, 0.62f, 0.70f);
    c[ImGuiCol_HeaderActive] = accent_hot;
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.26f, 0.30f, 1.00f);
    c[ImGuiCol_Tab] = panel;
    c[ImGuiCol_TabHovered] = accent;
    c[ImGuiCol_TabSelected] = ImVec4(0.14f, 0.40f, 0.48f, 1.00f);
    c[ImGuiCol_TabDimmed] = panel;
    c[ImGuiCol_TabDimmedSelected] = raised;
    c[ImGuiCol_DockingPreview] = ImVec4(0.18f, 0.64f, 0.76f, 0.5f);
    c[ImGuiCol_DockingEmptyBg] = bg;
    c[ImGuiCol_TableHeaderBg] = raised;
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.22f, 0.26f, 0.30f, 1.00f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.17f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
    c[ImGuiCol_CheckMark] = accent_hot;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent_hot;
    c[ImGuiCol_PlotLines] = accent_hot;
    c[ImGuiCol_PlotHistogram] = accent_hot;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = raised;
}

MainWindow::MainWindow(const viz::VisualizationConfig& config, const std::filesystem::path& shader_dir,
                       PerformanceMetrics& metrics)
    : image_panel_(config, shader_dir, metrics) {}

void MainWindow::buildDefaultLayout(unsigned int dockspace_id) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, vp->WorkSize);

    ImGuiID main = dockspace_id;
    const ImGuiID left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.20f, nullptr, &main);
    const ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.22f, nullptr, &main);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.26f, nullptr, &main);
    ImGui::DockBuilderDockWindow("Inspection###inspection", left);
    ImGui::DockBuilderDockWindow("Results###results", right);
    ImGui::DockBuilderDockWindow("Regions and log###bottom", bottom);
    ImGui::DockBuilderDockWindow("Main view###main", main);
    ImGui::DockBuilderFinish(dockspace_id);
}

void MainWindow::drawToolbar(GuiContext& context) {
    ViewState& vs = context.view;
    GuiInspectionController& c = context.controller;
    InspectionSettings& s = c.settings();

    if (ImGui::Button("Open reference")) {
        if (auto p = openFileDialog(context.native_window, "Open the reference image", imageFilters())) s.reference_path = *p;
    }
    ImGui::SameLine();
    if (ImGui::Button("Open inspected")) {
        if (auto p = openFileDialog(context.native_window, "Open the inspected image", imageFilters())) s.inspected_path = *p;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(c.inspectionRunning() || !c.validateSettings().empty());
    if (ImGui::Button("Run inspection")) {
        c.runInspection();
        vs.page = Page::Workspace;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !c.inspectionRunning()) {
        const std::string problem = c.validateSettings();
        if (!problem.empty()) ImGui::SetTooltip("%s", problem.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Load previous")) vs.page = Page::History;

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button("Reset view")) {
        vs.request_fit = true;
        vs.mode = viz::ViewMode::Annotated;
        vs.tool = viz::ViewportTool::Pan;
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit")) vs.request_fit = true;
    ImGui::SameLine();
    if (ImGui::Button("1:1")) vs.request_actual = true;

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Checkbox("Overlays", &vs.show_overlay);
    ImGui::SameLine();
    ImGui::Checkbox("Labels", &vs.show_labels);
    ImGui::SameLine();
    ImGui::Checkbox("Area", &vs.show_area);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    static const viz::ViewMode kModes[] = {viz::ViewMode::Reference,  viz::ViewMode::Inspected,   viz::ViewMode::Annotated,
                                           viz::ViewMode::Difference, viz::ViewMode::SideBySide,  viz::ViewMode::Blend};
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::BeginCombo("##mode", viz::toString(vs.mode))) {
        for (viz::ViewMode m : kModes) {
            if (ImGui::Selectable(viz::toString(m), vs.mode == m)) vs.mode = m;
        }
        ImGui::EndCombo();
    }
    if (vs.mode == viz::ViewMode::Blend) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("##blend", &vs.blend, 0.0f, 1.0f, "inspected %.2f");
    }

    // Navigation, right aligned.
    const float nav_width = 4 * 96.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20.0f, ImGui::GetWindowWidth() - nav_width - 14.0f));
    if (navButton("Workspace", vs.page == Page::Workspace)) vs.page = Page::Workspace;
    ImGui::SameLine();
    if (navButton("History", vs.page == Page::History)) vs.page = Page::History;
    ImGui::SameLine();
    if (navButton("Analytics", vs.page == Page::Analytics)) vs.page = Page::Analytics;
    ImGui::SameLine();
    if (navButton("Performance", vs.page == Page::Performance)) vs.page = Page::Performance;
}

void MainWindow::draw(GuiContext& context) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    constexpr ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                            ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar |
                                            ImGuiWindowFlags_NoBackground;
    ImGui::Begin("##host", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    drawToolbar(context);
    ImGui::Separator();

    const ImGuiID dockspace_id = ImGui::GetID("OptiInspectDock");
    if (!layout_built_) {
        buildDefaultLayout(dockspace_id);
        layout_built_ = true;
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();

    if (ImGui::Begin("Inspection###inspection")) inspection_panel_.draw(context);
    ImGui::End();

    if (ImGui::Begin("Results###results")) results_panel_.draw(context);
    ImGui::End();

    if (ImGui::Begin("Regions and log###bottom")) defect_panel_.draw(context);
    ImGui::End();

    // The workspace page draws nothing behind the image area: OpenGL renders there.
    ImGuiWindowFlags main_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (context.view.page == Page::Workspace) main_flags |= ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    if (ImGui::Begin("Main view###main", nullptr, main_flags)) {
        switch (context.view.page) {
            case Page::Workspace: image_panel_.draw(context); break;
            case Page::History: history_panel_.draw(context); break;
            case Page::Analytics: analytics_panel_.draw(context); break;
            case Page::Performance: performance_panel_.draw(context); break;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

}  // namespace optinspect::gui
