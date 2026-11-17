#include "HistoryPanel.hpp"

#include "NativeFileDialog.hpp"
#include "ResultsPanel.hpp"

#include <imgui.h>

#include <cstdio>
#include <filesystem>

namespace optinspect::gui {

namespace fs = std::filesystem;

namespace {

bool textField(const char* label, std::string& value, float width, const char* hint = "") {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputTextWithHint(label, hint, buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

}  // namespace

void HistoryPanel::draw(GuiContext& context) {
    GuiInspectionController& c = context.controller;
    InspectionSettings& settings = c.settings();
    FilterSettings& f = c.historyFilter();

    // Database selection.
    ImGui::TextUnformatted("Database");
    ImGui::SameLine();
    textField("##history_db", settings.database_path, ImGui::GetContentRegionAvail().x - 220.0f);
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        std::string start = fs::path(settings.database_path).parent_path().string();
        if (auto p = openFileDialog(context.native_window, "Select the SQLite database",
                                    {{"SQLite database", "*.db;*.sqlite;*.sqlite3"}, {"All files", "*.*"}}, start)) {
            settings.database_path = *p;
        }
    }
    ImGui::SameLine();
    const bool refresh_clicked = ImGui::Button("Connect / refresh");

    // Filters.
    static const char* kStatus[] = {"Any status", "PASS", "FAIL", "REVIEW"};
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("##status", &f.status, kStatus, 4);
    ImGui::SameLine();

    const HistoryResult* history = c.history();
    std::string product_preview = "Any product";
    if (!f.product_id_text.empty()) product_preview = "Product " + f.product_id_text;
    if (history) {
        for (const database::Product& p : history->products) {
            if (std::to_string(p.id) == f.product_id_text) product_preview = p.code + " - " + p.name;
        }
    }
    ImGui::SetNextItemWidth(190.0f);
    if (ImGui::BeginCombo("##product_filter", product_preview.c_str())) {
        if (ImGui::Selectable("Any product", f.product_id_text.empty())) f.product_id_text.clear();
        if (history) {
            for (const database::Product& p : history->products) {
                const std::string label = p.code + " - " + p.name;
                if (ImGui::Selectable(label.c_str(), std::to_string(p.id) == f.product_id_text)) {
                    f.product_id_text = std::to_string(p.id);
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    textField("##serial_filter", f.serial_number, 130.0f, "Serial number");
    ImGui::SameLine();
    textField("##from", f.date_from, 100.0f, "From YYYY-MM-DD");
    ImGui::SameLine();
    textField("##to", f.date_to, 100.0f, "To YYYY-MM-DD");
    textField("##idsearch", f.id_search, 150.0f, "Search inspection ID");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputInt("##limit", &f.limit, 0, 0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum number of rows to load");
    if (f.limit < 1) f.limit = 1;
    ImGui::SameLine();
    const bool apply = ImGui::Button("Apply");

    const bool database_changed = settings.database_path != last_database_;
    if (first_draw_ || refresh_clicked || apply || (database_changed && !c.historyBusy())) {
        std::error_code ec;
        if (fs::is_regular_file(settings.database_path, ec)) c.refreshHistory();
        last_database_ = settings.database_path;
        first_draw_ = false;
    }

    // Status line.
    if (c.historyBusy()) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.3f, 1.0f), "Querying the database...");
    } else if (!c.historyError().empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "Database error: %s", c.historyError().c_str());
    } else if (history) {
        ImGui::TextDisabled("%d matching inspection(s), %zu shown   -   query %.1f ms", history->matching_count,
                            history->inspections.size(), history->query_ms);
    } else {
        ImGui::TextDisabled("Select a database and press Connect / refresh.");
    }
    ImGui::Separator();
    if (!history) return;

    const float details_height = 210.0f;
    const float table_height = std::max(120.0f, ImGui::GetContentRegionAvail().y - details_height);
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##history", 8, flags, ImVec2(0.0f, table_height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Inspection ID", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Timestamp (UTC)", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Regions", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("Serial");
        ImGui::TableSetupColumn("Product");
        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableHeadersRow();

        for (const database::InspectionRecord& r : history->inspections) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool selected = c.selectedHistoryId() && *c.selectedHistoryId() == r.id;
            char id[24];
            std::snprintf(id, sizeof(id), "%lld", static_cast<long long>(r.id));
            if (ImGui::Selectable(id, selected, ImGuiSelectableFlags_SpanAllColumns)) c.selectInspection(r.id);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.external_id ? r.external_id->c_str() : "-");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.inspection_timestamp.c_str());
            ImGui::TableNextColumn();
            const std::string status = database::toString(r.status);
            ImGui::PushStyleColor(ImGuiCol_Text, decisionColor(status));
            ImGui::TextUnformatted(status.c_str());
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::Text("%d", r.detected_defect_count);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.serial_number ? r.serial_number->c_str() : "-");
            ImGui::TableNextColumn();
            if (r.product_id) ImGui::Text("%lld", static_cast<long long>(*r.product_id));
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (r.processing_time_ms) ImGui::Text("%.1f", *r.processing_time_ms);
            else ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    }

    // Details of the selected inspection (the view that was loaded from the database).
    ImGui::BeginChild("##history_details", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::SeparatorText("Selected inspection");
    const InspectionView& v = c.view();
    if (c.selectionLoading()) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.3f, 1.0f), "Loading inspection and images...");
    } else if (v.source == ResultSource::History && c.selectedHistoryId() && v.db_id == c.selectedHistoryId()) {
        ImGui::TextColored(ImColor(decisionColor(v.decision)), "%s", v.decision.c_str());
        ImGui::SameLine();
        ImGui::Text(" %s   -   %zu region(s)   -   alignment %s", v.external_id.c_str(), v.defects.size(),
                    v.alignment_succeeded ? (*v.alignment_succeeded ? "succeeded" : "failed") : "n/a");
        if (!v.decision_reason.empty()) ImGui::TextWrapped("Reason: %s", v.decision_reason.c_str());
        const auto fileLine = [](const char* label, const ImageSlot& slot) {
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(90.0f);
            if (slot.available()) ImGui::TextUnformatted(slot.path.c_str());
            else ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "%s", slot.error.c_str());
        };
        fileLine("Reference", v.images.reference);
        fileLine("Inspected", v.images.inspected);
        fileLine("Annotated", v.images.annotated);
        fileLine("Difference", v.images.difference);
        if (ImGui::Button("Open in workspace")) context.view.page = Page::Workspace;
    } else {
        ImGui::TextDisabled("Select a row to load the inspection, its images and its region measurements.");
    }
    ImGui::EndChild();
}

}  // namespace optinspect::gui
