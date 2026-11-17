#include "AnalyticsPanel.hpp"

#include "ResultsPanel.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace optinspect::gui {

namespace fs = std::filesystem;

namespace {

struct Bar {
    std::string label;
    double value = 0.0;
    std::string value_text;
    ImU32 color = IM_COL32(90, 140, 200, 255);
};

/// Horizontal bar chart with labels and numeric values. `max_value <= 0` is
/// handled (empty / all-zero data draws empty tracks, never divides by zero).
void drawBarChart(const char* title, const std::vector<Bar>& bars, double max_value, float width) {
    ImGui::TextUnformatted(title);
    if (bars.empty()) {
        ImGui::TextDisabled("No data.");
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float row_h = 22.0f;
    const float label_w = 120.0f;
    const float value_w = 90.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float track_w = std::max(40.0f, width - label_w - value_w);
    for (std::size_t i = 0; i < bars.size(); ++i) {
        const float y = origin.y + static_cast<float>(i) * row_h;
        dl->AddText(ImVec2(origin.x, y + 3.0f), IM_COL32(200, 206, 214, 255), bars[i].label.c_str());
        dl->AddRectFilled(ImVec2(origin.x + label_w, y + 3.0f), ImVec2(origin.x + label_w + track_w, y + row_h - 3.0f),
                          IM_COL32(34, 40, 48, 255), 3.0f);
        const double fraction = max_value > 0.0 ? std::clamp(bars[i].value / max_value, 0.0, 1.0) : 0.0;
        if (fraction > 0.0) {
            dl->AddRectFilled(ImVec2(origin.x + label_w, y + 3.0f),
                              ImVec2(origin.x + label_w + track_w * static_cast<float>(fraction), y + row_h - 3.0f),
                              bars[i].color, 3.0f);
        }
        dl->AddText(ImVec2(origin.x + label_w + track_w + 8.0f, y + 3.0f), IM_COL32(220, 226, 232, 255),
                    bars[i].value_text.c_str());
    }
    ImGui::Dummy(ImVec2(width, row_h * static_cast<float>(bars.size()) + 4.0f));
}

std::string fmt(const char* format, double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), format, v);
    return buf;
}

std::string countAndRate(int count, double rate) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d (%.1f%%)", count, rate * 100.0);
    return buf;
}

/// Stacked columns (PASS / FAIL / REVIEW) per period.
void drawTrend(const std::vector<database::PeriodStatistics>& trend, float width, float height) {
    ImGui::TextUnformatted("Inspections per period (PASS / FAIL / REVIEW)");
    if (trend.empty()) {
        ImGui::TextDisabled("No data.");
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float axis_h = 36.0f;
    const float plot_h = height - axis_h;
    int max_total = 1;
    for (const auto& p : trend) max_total = std::max(max_total, p.total);

    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + plot_h), IM_COL32(26, 31, 38, 255), 3.0f);
    const std::size_t n = trend.size();
    const float slot = width / static_cast<float>(n);
    const float bar_w = std::min(46.0f, slot * 0.7f);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& p = trend[i];
        const float x = origin.x + slot * static_cast<float>(i) + (slot - bar_w) * 0.5f;
        float y = origin.y + plot_h;
        const auto segment = [&](int count, ImU32 color) {
            if (count <= 0) return;
            const float h = plot_h * static_cast<float>(count) / static_cast<float>(max_total);
            dl->AddRectFilled(ImVec2(x, y - h), ImVec2(x + bar_w, y), color);
            y -= h;
        };
        segment(p.pass, decisionColor("PASS"));
        segment(p.fail, decisionColor("FAIL"));
        segment(p.review, decisionColor("REVIEW"));
        char total[16];
        std::snprintf(total, sizeof(total), "%d", p.total);
        const ImVec2 ts = ImGui::CalcTextSize(total);
        dl->AddText(ImVec2(x + (bar_w - ts.x) * 0.5f, y - ts.y - 2.0f), IM_COL32(230, 235, 240, 255), total);
        // Label every period when there is room, otherwise every few.
        const std::size_t stride = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(90.0f / slot)));
        if (i % stride == 0) {
            dl->AddText(ImVec2(x - 14.0f, origin.y + plot_h + 4.0f), IM_COL32(170, 178, 188, 255), p.period.c_str());
        }
    }
    ImGui::Dummy(ImVec2(width, height));
}

}  // namespace

void AnalyticsPanel::draw(GuiContext& context) {
    GuiInspectionController& c = context.controller;
    InspectionSettings& settings = c.settings();
    FilterSettings& f = c.analyticsFilter();

    ImGui::TextUnformatted("Database");
    ImGui::SameLine();
    {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", settings.database_path.c_str());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120.0f);
        if (ImGui::InputText("##analytics_db", buf, sizeof(buf))) settings.database_path = buf;
    }
    ImGui::SameLine();
    const bool refresh = ImGui::Button("Refresh");

    static const char* kStatus[] = {"Any status", "PASS", "FAIL", "REVIEW"};
    static const char* kGranularity[] = {"Per day", "Per week", "Per month"};
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("##a_status", &f.status, kStatus, 4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::Combo("##a_gran", &f.granularity, kGranularity, 3);
    ImGui::SameLine();
    const auto field = [](const char* id, std::string& value, float w, const char* hint) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", value.c_str());
        ImGui::SetNextItemWidth(w);
        if (ImGui::InputTextWithHint(id, hint, buf, sizeof(buf))) value = buf;
    };
    field("##a_prod", f.product_id_text, 100.0f, "Product ID");
    ImGui::SameLine();
    field("##a_serial", f.serial_number, 120.0f, "Serial number");
    ImGui::SameLine();
    field("##a_from", f.date_from, 110.0f, "From YYYY-MM-DD");
    ImGui::SameLine();
    field("##a_to", f.date_to, 110.0f, "To YYYY-MM-DD");
    ImGui::SameLine();
    const bool apply = ImGui::Button("Apply filters");

    if (first_draw_ || refresh || apply || settings.database_path != last_database_) {
        std::error_code ec;
        if (fs::is_regular_file(settings.database_path, ec) && !c.analyticsBusy()) c.refreshAnalytics();
        last_database_ = settings.database_path;
        first_draw_ = false;
    }

    if (c.analyticsBusy()) ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.3f, 1.0f), "Computing analytics...");
    else if (!c.analyticsError().empty())
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "Database error: %s", c.analyticsError().c_str());
    ImGui::Separator();

    const AnalyticsSnapshot* a = c.analytics();
    if (!a) {
        ImGui::TextDisabled("No analytics loaded. Select a database with stored inspections.");
        return;
    }

    ImGui::BeginChild("##analytics_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    const float width = std::max(320.0f, ImGui::GetContentRegionAvail().x - 8.0f);
    const float half = (width - 20.0f) * 0.5f;

    // Summary cards.
    ImGui::Text("Inspections in scope: %d", a->summary.total);
    ImGui::SameLine(0, 30);
    ImGui::Text("Detected regions: %d", a->defects.total_defects);
    ImGui::SameLine(0, 30);
    ImGui::Text("Avg regions / inspection: %.2f", a->defects.avg_defects_per_inspection);
    ImGui::SameLine(0, 30);
    ImGui::Text("Max in one inspection: %d", a->defects.max_defects_in_inspection);
    ImGui::TextDisabled("Detected regions are detector output, not confirmed defects. No accuracy (false positive / negative) figures exist without ground truth.");
    ImGui::Spacing();

    if (ImGui::BeginTable("##analytics_cols", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        {
            std::vector<Bar> status = {
                {"PASS", static_cast<double>(a->summary.pass), countAndRate(a->summary.pass, a->summary.pass_rate), decisionColor("PASS")},
                {"FAIL", static_cast<double>(a->summary.fail), countAndRate(a->summary.fail, a->summary.fail_rate), decisionColor("FAIL")},
                {"REVIEW", static_cast<double>(a->summary.review), countAndRate(a->summary.review, a->summary.review_rate), decisionColor("REVIEW")}};
            drawBarChart("Status distribution", status, static_cast<double>(std::max(1, a->summary.total)), half);
        }
        ImGui::TableNextColumn();
        {
            std::vector<Bar> times;
            const auto addTime = [&](const std::string& label, const database::ProcessingTimeStats& s) {
                if (s.avg_ms) times.push_back({label, *s.avg_ms, fmt("%.1f ms avg", *s.avg_ms), IM_COL32(90, 140, 200, 255)});
            };
            addTime("All", a->processing);
            for (const auto& g : a->processing_by_status) addTime(g.group, g.stats);
            double max_time = 0.0;
            for (const Bar& b : times) max_time = std::max(max_time, b.value);
            drawBarChart("Average processing time", times, max_time, half);
            if (a->processing.avg_ms) {
                ImGui::TextDisabled("min %.1f ms   avg %.1f ms   max %.1f ms   (%d samples)",
                                    a->processing.min_ms.value_or(0.0), *a->processing.avg_ms,
                                    a->processing.max_ms.value_or(0.0), a->processing.samples);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    drawTrend(a->trend, width, 200.0f);
    ImGui::TextDisabled("PASS  FAIL  REVIEW stacked, newest period on the right.");

    ImGui::Spacing();
    {
        std::vector<Bar> products;
        for (const auto& p : a->products) {
            char label[96];
            std::snprintf(label, sizeof(label), "%s", p.product_code.c_str());
            char value[64];
            std::snprintf(value, sizeof(value), "%.1f%%  (%d/%d)", p.failure_rate * 100.0, p.failed, p.total);
            products.push_back({label, p.failure_rate, value, decisionColor("FAIL")});
        }
        drawBarChart("Failure rate by product (FAIL / inspections)", products, 1.0, width);
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Tables", ImGuiTreeNodeFlags_None)) {
        if (ImGui::BeginTable("##trend_table", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            for (const char* h : {"Period", "Total", "PASS", "FAIL", "REVIEW", "Fail rate", "Regions/insp.", "Cumulative"})
                ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (const auto& p : a->trend) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(p.period.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%d", p.total);
                ImGui::TableNextColumn(); ImGui::Text("%d", p.pass);
                ImGui::TableNextColumn(); ImGui::Text("%d", p.fail);
                ImGui::TableNextColumn(); ImGui::Text("%d", p.review);
                ImGui::TableNextColumn(); ImGui::Text("%.1f%%", p.fail_rate * 100.0);
                ImGui::TableNextColumn(); ImGui::Text("%.2f", p.avg_defects_per_inspection);
                ImGui::TableNextColumn(); ImGui::Text("%d", p.cumulative_inspections);
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

}  // namespace optinspect::gui
