#include "ResultsPanel.hpp"

#include <imgui.h>

#include <cstdio>

namespace optinspect::gui {

unsigned int decisionColor(const std::string& decision) {
    if (decision == "PASS") return IM_COL32(52, 168, 83, 255);
    if (decision == "FAIL") return IM_COL32(217, 63, 52, 255);
    if (decision == "REVIEW") return IM_COL32(242, 153, 0, 255);
    return IM_COL32(110, 120, 130, 255);
}

namespace {

void keyValue(const char* key, const std::string& value) {
    ImGui::TextDisabled("%s", key);
    ImGui::SameLine(118.0f);
    ImGui::TextUnformatted(value.empty() ? "-" : value.c_str());
}

}  // namespace

void ResultsPanel::draw(GuiContext& context) {
    const InspectionView& v = context.controller.view();
    if (!v.hasResult()) {
        ImGui::TextDisabled("No result yet.");
        ImGui::TextWrapped("Run an inspection or load a stored one from the History page.");
        return;
    }

    // Decision badge.
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 46.0f), decisionColor(v.decision), 5.0f);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.7f);
    const ImVec2 big = ImGui::CalcTextSize(v.decision.c_str());
    dl->AddText(ImVec2(pos.x + (width - big.x) * 0.5f, pos.y + (46.0f - big.y) * 0.5f), IM_COL32(255, 255, 255, 255),
                v.decision.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(width, 50.0f));

    if (!v.decision_reason.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", v.decision_reason.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();

    keyValue("Source", v.source == ResultSource::History ? "Stored inspection" : "Live inspection");
    keyValue("Inspection ID", v.external_id);
    keyValue("Timestamp (UTC)", v.timestamp);
    keyValue("Database ID", v.db_id ? std::to_string(*v.db_id) : std::string());
    keyValue("Product", v.product_label ? *v.product_label
                                         : (v.product_id ? "ID " + std::to_string(*v.product_id) : std::string()));
    keyValue("Serial number", v.serial_number.value_or(""));

    ImGui::SeparatorText("Alignment");
    if (v.alignment_succeeded) {
        ImGui::TextDisabled("Status");
        ImGui::SameLine(118.0f);
        ImGui::TextColored(*v.alignment_succeeded ? ImVec4(0.45f, 0.85f, 0.55f, 1.0f) : ImVec4(0.95f, 0.4f, 0.35f, 1.0f),
                           "%s", *v.alignment_succeeded ? "Succeeded" : "Failed");
    } else {
        keyValue("Status", "");
    }
    char buf[64];
    if (v.alignment_confidence) std::snprintf(buf, sizeof(buf), "%.3f", *v.alignment_confidence);
    else buf[0] = '\0';
    keyValue("Confidence", buf);

    ImGui::SeparatorText("Detected regions");
    ImGui::Text("%zu region(s) detected", v.defects.size());
    ImGui::TextDisabled("Candidate regions found by the detector;\nnot confirmed manufacturing defects.");
    if (!v.defects.empty() &&
        ImGui::BeginChild("##region_list", ImVec2(0.0f, std::min(150.0f, 22.0f * v.defects.size() + 6.0f)), ImGuiChildFlags_Borders)) {
        for (const DefectRow& d : v.defects) {
            char label[96];
            std::snprintf(label, sizeof(label), "#%d   %d x %d px   A=%.0f", d.index, d.width, d.height, d.area_px);
            const bool selected = context.view.selected_defect && *context.view.selected_defect == d.index;
            if (ImGui::Selectable(label, selected)) {
                context.view.selected_defect = d.index;
                context.view.request_center_selected = context.view.center_on_select;
            }
        }
    }
    if (!v.defects.empty()) ImGui::EndChild();

    ImGui::SeparatorText("Timing");
    if (v.processing_time_ms) {
        std::snprintf(buf, sizeof(buf), "%.1f ms", *v.processing_time_ms);
        keyValue("Processing time", buf);
    }
    if (!v.stages.empty() && ImGui::BeginTable("##stages", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        for (const StageTimingRow& s : v.stages) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.2f ms", s.ms);
        }
        ImGui::EndTable();
    } else if (v.stages.empty()) {
        ImGui::TextDisabled("No stage timings available.");
    }

    if (!v.warnings.empty()) {
        ImGui::SeparatorText("Warnings");
        ImGui::PushTextWrapPos(0.0f);
        for (const std::string& w : v.warnings) ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.3f, 1.0f), "%s", w.c_str());
        ImGui::PopTextWrapPos();
    }
}

}  // namespace optinspect::gui
