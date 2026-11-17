#include "InspectionPanel.hpp"

#include "NativeFileDialog.hpp"

#include <imgui.h>

#include <filesystem>

namespace optinspect::gui {

namespace fs = std::filesystem;

namespace {

// Table-free helper: a labelled, full-width text field with a Browse button.
bool inputText(const char* id, std::string& value) {
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    ImGui::SetNextItemWidth(-72.0f);
    if (ImGui::InputText(id, buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

}  // namespace

bool InspectionPanel::pathRow(GuiContext& context, const char* label, std::string& value, bool is_folder,
                              const char* dialog_title) {
    ImGui::TextUnformatted(label);
    ImGui::PushID(label);
    bool changed = inputText("##path", value);
    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
        std::string start = value;
        std::error_code ec;
        if (!start.empty() && !fs::is_directory(start, ec)) start = fs::path(start).parent_path().string();
        std::optional<std::string> picked;
        if (is_folder) {
            picked = pickFolderDialog(context.native_window, dialog_title, start);
        } else {
            const bool is_config = std::string(label).find("Config") != std::string::npos;
            const bool is_database = std::string(label).find("Database") != std::string::npos;
            std::vector<FileFilter> filters;
            if (is_config) filters = {{"JSON configuration", "*.json"}, {"All files", "*.*"}};
            else if (is_database) filters = {{"SQLite database", "*.db;*.sqlite;*.sqlite3"}, {"All files", "*.*"}};
            else filters = {{"Images", "*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff"}, {"All files", "*.*"}};
            picked = openFileDialog(context.native_window, dialog_title, filters, start);
        }
        if (picked) {
            value = *picked;
            changed = true;
        }
    }
    ImGui::PopID();

    // Inline validation of file paths (a database file may legitimately not exist yet: it is created on first use).
    if (!is_folder && !value.empty() && std::string(label).find("Database") == std::string::npos) {
        std::error_code ec;
        if (!fs::is_regular_file(value, ec)) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "File not found");
    }
    return changed;
}

void InspectionPanel::draw(GuiContext& context) {
    GuiInspectionController& c = context.controller;
    InspectionSettings& s = c.settings();

    ImGui::SeparatorText("Images");
    pathRow(context, "Reference image", s.reference_path, false, "Select the reference (golden) image");
    pathRow(context, "Inspected image", s.inspected_path, false, "Select the inspected image");

    ImGui::SeparatorText("Configuration and output");
    pathRow(context, "Configuration file", s.config_path, false, "Select the inspection configuration");
    pathRow(context, "Output directory", s.output_dir, true, "Select the output directory");

    ImGui::SeparatorText("Database (Phase 2)");
    ImGui::Checkbox("Store result in the database", &s.persist);
    pathRow(context, "Database path", s.database_path, false, "Select the SQLite database");
    ImGui::TextUnformatted("Product ID (optional)");
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s", s.product_id_text.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##product", buf, sizeof(buf), ImGuiInputTextFlags_CharsDecimal)) s.product_id_text = buf;
    }
    ImGui::TextUnformatted("Serial number (optional)");
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", s.serial_number.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##serial", buf, sizeof(buf))) s.serial_number = buf;
    }

    ImGui::SeparatorText("Execution");
    const bool running = c.inspectionRunning();
    const std::string problem = running ? std::string() : c.validateSettings();

    ImGui::BeginDisabled(running || !problem.empty());
    if (ImGui::Button("Run inspection", ImVec2(-1.0f, 34.0f))) c.runInspection();
    ImGui::EndDisabled();

    if (running) {
        // The pipeline reports stages but no percentage, so the bar is indeterminate on purpose.
        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-1.0f, 0.0f), "");
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.3f, 1.0f), "%s", toString(c.workerPhase()));
    } else if (!problem.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.3f, 1.0f), "%s", problem.c_str());
    } else if (!c.lastInspectionError().empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "Failed: %s", c.lastInspectionError().c_str());
    } else if (c.view().source == ResultSource::LiveInspection) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "Completed");
        if (c.view().db_id) {
            ImGui::Text("Database inspection ID: %lld", static_cast<long long>(*c.view().db_id));
        }
    } else {
        ImGui::TextDisabled("Ready");
    }
    if (!c.lastPersistenceError().empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "Not stored in the database: %s",
                           c.lastPersistenceError().c_str());
        ImGui::PopTextWrapPos();
    }
}

}  // namespace optinspect::gui
