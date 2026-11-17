#include "PerformancePanel.hpp"

#include <imgui.h>

#include <cstdio>
#include <vector>

namespace optinspect::gui {

namespace {

void row(const char* label, const char* value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value);
}

void statRow(const char* label, const RollingStat& s) {
    char buf[160];
    if (s.empty()) {
        std::snprintf(buf, sizeof(buf), "no samples yet");
    } else {
        std::snprintf(buf, sizeof(buf), "last %.2f   avg %.2f   max(recent) %.2f   max(ever) %.2f ms   [%zu samples]",
                      s.last(), s.average(), s.maxRecent(), s.maxEver(), s.totalCount());
    }
    row(label, buf);
}

void plot(const char* id, const RollingStat& s, float max_value) {
    std::vector<float> values;
    for (double v : s.samples()) values.push_back(static_cast<float>(v));
    if (values.empty()) return;
    ImGui::PlotLines(id, values.data(), static_cast<int>(values.size()), 0, nullptr, 0.0f, max_value,
                     ImVec2(ImGui::GetContentRegionAvail().x, 54.0f));
}

}  // namespace

void PerformancePanel::draw(GuiContext& context) {
    ImGui::BeginChild("##performance_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    drawContent(context);
    ImGui::EndChild();
}

void PerformancePanel::drawContent(GuiContext& context) {
    const PerformanceMetrics& m = context.metrics;
    const viz::GLInfo& gl = context.gl_info;
    char buf[200];

    ImGui::SeparatorText("OpenGL");
    if (ImGui::BeginTable("##gl", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        row("Version", gl.version.c_str());
        row("Renderer", gl.renderer.c_str());
        row("Vendor", gl.vendor.c_str());
        row("GLSL", gl.glsl_version.c_str());
        std::snprintf(buf, sizeof(buf), "%d", gl.max_texture_size);
        row("Max texture size", buf);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Startup");
    if (ImGui::BeginTable("##startup", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        std::snprintf(buf, sizeof(buf), "%.1f ms", m.startup_ms);
        row("Application startup (to first frame)", buf);
        std::snprintf(buf, sizeof(buf), "%.1f ms", m.gl_init_ms);
        row("GLFW window + GL context + GLAD", buf);
        std::snprintf(buf, sizeof(buf), "%.1f ms", m.renderer_init_ms);
        row("Shaders, VAO/VBO, ImGui backends", buf);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Frame time (separated)");
    if (ImGui::BeginTable("##frames", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        statRow("Frame (incl. swap / vsync wait)", m.frame_ms);
        statRow("Frame CPU time (no swap)", m.cpu_frame_ms);
        statRow("UI build (ImGui, CPU)", m.ui_build_ms);
        statRow("Image + overlay GL submit (CPU)", m.gl_submit_ms);
        statRow("Image + overlay render (GPU)", m.gpu_render_ms);
        statRow("ImGui draw-data render (CPU)", m.imgui_render_ms);
        ImGui::EndTable();
    }
    plot("##frame_plot", m.frame_ms, 40.0f);

    ImGui::SeparatorText("Images and textures");
    if (ImGui::BeginTable("##tex", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        statRow("Texture upload (CPU, per image)", m.texture_upload_ms);
        if (!m.last_upload.label.empty()) {
            std::snprintf(buf, sizeof(buf), "%.2f ms, %.2f MB  (%s)", m.last_upload.ms,
                          static_cast<double>(m.last_upload.bytes) / (1024.0 * 1024.0), m.last_upload.label.c_str());
            row("Last upload", buf);
        }
        statRow("Image decode (worker thread)", m.image_load_ms);
        std::snprintf(buf, sizeof(buf), "%.2f MB in %zu texture(s)", static_cast<double>(m.texture_bytes) / (1024.0 * 1024.0),
                      m.texture_count);
        row("GPU texture memory (approx., with mips)", buf);
        std::snprintf(buf, sizeof(buf), "%zu hits / %zu misses / %zu evictions", m.cache_hits, m.cache_misses,
                      m.cache_evictions);
        row("Texture cache", buf);
        std::snprintf(buf, sizeof(buf), "%zu", m.images_skipped_cached);
        row("Decodes skipped (texture already cached)", buf);
        std::snprintf(buf, sizeof(buf), "%zu glTexImage2D / %zu glTexSubImage2D", m.texture_allocations,
                      m.texture_updates);
        row("Texture allocations / replacements", buf);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Background work");
    if (ImGui::BeginTable("##bg", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        statRow("Inspection job (worker thread)", m.inspection_worker_ms);
        statRow("Database query", m.db_query_ms);
        statRow("UI frame time while an inspection ran", m.frame_ms_during_inspection);
        ImGui::EndTable();
    }
    ImGui::TextDisabled("CPU usage and GPU utilization are not sampled by the application; use the Windows Task Manager / GPU tools.");
}

}  // namespace optinspect::gui
