#include "DefectPanel.hpp"

#include <imgui.h>

#include <cstdio>

namespace optinspect::gui {

namespace {

void optionalCell(const std::optional<double>& value, const char* format) {
    if (value) ImGui::Text(format, *value);
    else ImGui::TextDisabled("-");
}

}  // namespace

void DefectPanel::draw(GuiContext& context) {
    if (ImGui::BeginTabBar("##bottom_tabs")) {
        const std::size_t count = context.controller.view().defects.size();
        char title[48];
        std::snprintf(title, sizeof(title), "Regions (%zu)###regions", count);
        if (ImGui::BeginTabItem(title)) {
            drawTable(context);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Log")) {
            drawLog(context);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void DefectPanel::drawTable(GuiContext& context) {
    const InspectionView& v = context.controller.view();
    if (!v.hasResult()) {
        ImGui::TextDisabled("No inspection loaded.");
        return;
    }
    if (v.defects.empty()) {
        ImGui::TextDisabled("No regions were detected in this inspection.");
        return;
    }

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##defects", 11, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Bounding box (x, y, w, h)", ImGuiTableColumnFlags_WidthStretch, 2.2f);
    ImGui::TableSetupColumn("X");
    ImGui::TableSetupColumn("Y");
    ImGui::TableSetupColumn("Width");
    ImGui::TableSetupColumn("Height");
    ImGui::TableSetupColumn("Area (px\xc2\xb2)");
    ImGui::TableSetupColumn("Perimeter (px)");
    ImGui::TableSetupColumn("Centroid (x, y)", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Aspect");
    ImGui::TableSetupColumn("Score");
    ImGui::TableHeadersRow();

    for (const DefectRow& d : v.defects) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = context.view.selected_defect && *context.view.selected_defect == d.index;
        char id[16];
        std::snprintf(id, sizeof(id), "%d", d.index);
        if (ImGui::Selectable(id, selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
            context.view.selected_defect = d.index;
            context.view.request_center_selected = context.view.center_on_select;
        }
        ImGui::TableNextColumn();
        ImGui::Text("(%d, %d, %d, %d)", d.x, d.y, d.width, d.height);
        ImGui::TableNextColumn();
        ImGui::Text("%d", d.x);
        ImGui::TableNextColumn();
        ImGui::Text("%d", d.y);
        ImGui::TableNextColumn();
        ImGui::Text("%d", d.width);
        ImGui::TableNextColumn();
        ImGui::Text("%d", d.height);
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", d.area_px);
        ImGui::TableNextColumn();
        optionalCell(d.perimeter_px, "%.1f");
        ImGui::TableNextColumn();
        if (d.centroid_x && d.centroid_y) ImGui::Text("(%.1f, %.1f)", *d.centroid_x, *d.centroid_y);
        else ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        optionalCell(d.aspect_ratio, "%.2f");
        ImGui::TableNextColumn();
        optionalCell(d.confidence, "%.2f");
    }
    ImGui::EndTable();
}

void DefectPanel::drawLog(GuiContext& context) {
    const std::vector<LogEntry>& log = context.controller.log();
    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None);
    for (const LogEntry& e : log) {
        const ImVec4 color = e.level == LogLevel::Error     ? ImVec4(0.95f, 0.4f, 0.35f, 1.0f)
                             : e.level == LogLevel::Warning ? ImVec4(0.95f, 0.78f, 0.3f, 1.0f)
                                                            : ImVec4(0.78f, 0.82f, 0.86f, 1.0f);
        ImGui::TextColored(color, "%s", e.text.c_str());
    }
    if (log.size() != last_log_size_) {
        ImGui::SetScrollHereY(1.0f);
        last_log_size_ = log.size();
    }
    ImGui::EndChild();
}

}  // namespace optinspect::gui
