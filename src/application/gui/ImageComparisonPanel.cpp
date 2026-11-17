#include "ImageComparisonPanel.hpp"

#include "common/Timing.hpp"
#include "database/TimeUtil.hpp"
#include "third_party/nlohmann/json.hpp"

#include <glad/glad.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace optinspect::gui {

namespace fs = std::filesystem;

namespace {

ImVec2 toIm(viz::Vec2 v) { return ImVec2(static_cast<float>(v.x), static_cast<float>(v.y)); }

ImU32 toU32(const viz::Color4& c) {
    return IM_COL32(static_cast<int>(c.r * 255.f), static_cast<int>(c.g * 255.f), static_cast<int>(c.b * 255.f),
                    static_cast<int>(c.a * 255.f));
}

viz::RectD toRectD(const viz::RectI& r) {
    return {static_cast<double>(r.x), static_cast<double>(r.y), static_cast<double>(r.w), static_cast<double>(r.h)};
}

}  // namespace

ImageComparisonPanel::ImageComparisonPanel(const viz::VisualizationConfig& config,
                                           const std::filesystem::path& shader_dir, PerformanceMetrics& metrics)
    : config_(config),
      metrics_(metrics),
      image_renderer_(shader_dir, config),
      overlay_renderer_(shader_dir),
      cache_(config.texture_cache_budget_bytes),
      main_(config),
      left_(config),
      right_(config) {}

std::unordered_set<std::string> ImageComparisonPanel::cachedKeys() const {
    std::unordered_set<std::string> keys;
    for (const std::string& k : cache_.keys()) keys.insert(k);
    return keys;
}

void ImageComparisonPanel::uploadSlot(Role role, const ImageSlot& slot) {
    role_error_[role].clear();
    current_[role].reset();
    if (!slot.error.empty()) {
        role_error_[role] = slot.error;
        return;
    }
    if (slot.cache_key.empty()) {
        role_error_[role] = "Image not available.";
        return;
    }
    if (TexturePtr hit = cache_.get(slot.cache_key)) {
        current_[role] = hit;  // already on the GPU: no decode, no upload
        return;
    }
    if (!slot.pixels) {
        role_error_[role] = "The cached texture was evicted before it could be shown; select the inspection again.";
        return;
    }
    try {
        auto texture = std::make_shared<viz::Texture2D>();
        Stopwatch timer;
        texture->upload(*slot.pixels, true);
        if (sync_upload_timing_) glFinish();
        const double ms = timer.elapsedMilliseconds();
        metrics_.texture_upload_ms.add(ms);
        metrics_.last_upload = {slot.path, ms, texture->gpuBytes()};
        metrics_.texture_allocations += texture->allocationCount();
        metrics_.texture_updates += texture->updateCount();
        cache_.put(slot.cache_key, texture, texture->gpuBytes());
        current_[role] = texture;
    } catch (const std::exception& e) {
        role_error_[role] = e.what();
    }
}

void ImageComparisonPanel::syncTextures(const InspectionView& view) {
    if (view.generation == synced_generation_) return;
    synced_generation_ = view.generation;
    external_id_ = view.external_id;
    uploadSlot(Reference, view.images.reference);
    uploadSlot(Inspected, view.images.inspected);
    uploadSlot(Annotated, view.images.annotated);
    uploadSlot(Difference, view.images.difference);

    annotated_dir_.clear();
    const std::string& anchor = !view.images.annotated.path.empty() ? view.images.annotated.path : view.report_path;
    if (!anchor.empty()) annotated_dir_ = fs::path(anchor).parent_path().string();

    roi_.reset();
    main_.viewport.clearRoi();
    left_.viewport.clearRoi();
    right_.viewport.clearRoi();

    metrics_.cache_hits = cache_.stats().hits;
    metrics_.cache_misses = cache_.stats().misses;
    metrics_.cache_evictions = cache_.stats().evictions;
}

ImageComparisonPanel::TexturePtr ImageComparisonPanel::textureFor(viz::ViewMode mode) const {
    switch (mode) {
        case viz::ViewMode::Reference: return current_[Reference];
        case viz::ViewMode::Inspected: return current_[Inspected];
        case viz::ViewMode::Annotated: return current_[Annotated];
        case viz::ViewMode::Difference: return current_[Difference];
        default: return nullptr;
    }
}

std::string ImageComparisonPanel::unavailableReason(viz::ViewMode mode) const {
    const auto reason = [&](Role r, const char* name) -> std::string {
        if (current_[r]) return {};
        return role_error_[r].empty() ? std::string(name) + " image is not available." : role_error_[r];
    };
    switch (mode) {
        case viz::ViewMode::Reference: return reason(Reference, "Reference");
        case viz::ViewMode::Inspected: return reason(Inspected, "Inspected");
        case viz::ViewMode::Annotated: return reason(Annotated, "Annotated");
        case viz::ViewMode::Difference: return reason(Difference, "Difference");
        case viz::ViewMode::SideBySide:
        case viz::ViewMode::Blend: {
            std::string r = reason(Reference, "Reference");
            if (r.empty()) r = reason(Inspected, "Inspected");
            return r;
        }
    }
    return {};
}

void ImageComparisonPanel::drawToolStrip(GuiContext& context) {
    ViewState& vs = context.view;
    int tool = vs.tool == viz::ViewportTool::Pan ? 0 : 1;
    if (ImGui::RadioButton("Pan / select", tool == 0)) vs.tool = viz::ViewportTool::Pan;
    ImGui::SameLine();
    if (ImGui::RadioButton("ROI", tool == 1)) vs.tool = viz::ViewportTool::Roi;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag in the image to select a rectangular region of interest.\nA click without dragging clears it.");

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (roi_) {
        ImGui::Text("ROI  x=%d y=%d  %d x %d px  area=%lld px\xc2\xb2", roi_->x, roi_->y, roi_->w, roi_->h,
                    roi_->area());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear ROI")) vs.request_clear_roi = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Export ROI")) vs.request_export_roi = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Saves the ROI to <id>_roi.json next to the inspection images.\nThe inspection report is not modified.");
    } else {
        ImGui::TextDisabled("No ROI selected");
    }
}

void ImageComparisonPanel::exportRoi(GuiContext& context) {
    if (!roi_) return;
    const InspectionView& v = context.controller.view();
    std::string dir = annotated_dir_;
    if (dir.empty()) dir = context.controller.settings().output_dir;
    if (dir.empty()) dir = ".";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string id = external_id_.empty() ? "roi" : external_id_;
    const std::string path = (fs::path(dir) / (id + "_roi.json")).string();

    nlohmann::json j;
    j["inspection_id"] = v.external_id;
    j["created_utc"] = database::nowUtcTimestamp();
    j["coordinate_system"] = "image pixels of the displayed image, origin top-left, x right, y down";
    j["roi"] = {{"x", roi_->x}, {"y", roi_->y}, {"width", roi_->w}, {"height", roi_->h}, {"area_px", roi_->area()}};
    j["note"] = "Visualization artifact. It does not modify or re-run the inspection.";

    std::ofstream out(path);
    if (!out) {
        context.controller.addLog(LogLevel::Error, "Could not write ROI file: " + path);
        return;
    }
    out << j.dump(2) << "\n";
    context.controller.addLog(LogLevel::Info, "ROI saved to " + path);
    notice_ = "ROI saved: " + path;
}

void ImageComparisonPanel::handleInput(GuiContext& context, Pane& target, bool side_by_side) {
    ImGuiIO& io = ImGui::GetIO();
    ViewState& vs = context.view;

    viz::PointerState p;
    p.cursor = {io.MousePos.x, io.MousePos.y};
    p.hovered = true;
    p.wheel = ImGui::IsItemHovered() ? io.MouseWheel : 0.0;
    p.left_pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered();
    p.left_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    p.left_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    p.middle_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    p.delta = {io.MouseDelta.x, io.MouseDelta.y};

    for (Pane* pane : active_panes_) {
        pane->viewport.setTool(vs.tool);
    }

    for (Pane* pane : active_panes_) {
        if (pane == &target) {
            pane->viewport.update(p);
        } else {
            viz::PointerState idle;
            idle.cursor = p.cursor;
            pane->viewport.update(idle);
        }
    }

    roi_ = target.viewport.roi();

    if (side_by_side) {
        for (Pane* pane : active_panes_) {
            if (pane != &target) pane->viewport.transform().copyViewFrom(target.viewport.transform());
        }
    }

    if (const std::optional<viz::Vec2> click = target.viewport.takeClick()) {
        const double tolerance = 4.0 / std::max(target.viewport.transform().zoom(), 0.01);
        const std::optional<int> hit = viz::hitTestDefects(boxes_, *click, tolerance);
        vs.selected_defect = hit;
        if (hit && vs.center_on_select) vs.request_center_selected = true;
    }
}

void ImageComparisonPanel::buildOverlays(GuiContext& context, Pane& pane) {
    const ViewState& vs = context.view;
    pane.geometry.clear();
    if (vs.show_overlay && !boxes_.empty()) {
        pane.geometry = viz::buildDefectOverlay(boxes_, vs.selected_defect, pane.viewport.transform(), config_.overlay);
    }
    if (const std::optional<viz::RectI> roi = pane.viewport.roiForDisplay()) {
        viz::appendRoi(pane.geometry, pane.viewport.transform(), toRectD(*roi), config_.overlay);
    }

    if (vs.show_overlay && vs.show_labels) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(toIm({pane.rect.x, pane.rect.y}), toIm({pane.rect.right(), pane.rect.bottom()}), true);
        for (const viz::OverlayLabel& label : pane.geometry.labels) {
            char text[48];
            if (vs.show_area) std::snprintf(text, sizeof(text), "#%d  A=%.0f", label.index, label.area_px);
            else std::snprintf(text, sizeof(text), "#%d", label.index);
            const ImVec2 size = ImGui::CalcTextSize(text);
            ImVec2 pos(static_cast<float>(label.screen_pos.x), static_cast<float>(label.screen_pos.y) - size.y - 4.0f);
            if (pos.y < pane.rect.y) pos.y = static_cast<float>(label.screen_pos.y) + 4.0f;
            const ImU32 fg = label.selected ? toU32(config_.overlay.selected_outline) : IM_COL32(255, 235, 230, 255);
            dl->AddRectFilled(ImVec2(pos.x - 2, pos.y - 1), ImVec2(pos.x + size.x + 2, pos.y + size.y + 1),
                              IM_COL32(10, 12, 16, 190));
            dl->AddText(pos, fg, text);
        }
        dl->PopClipRect();
    }
}

void ImageComparisonPanel::drawStatusLine(const GuiContext& context, const Pane& pane) {
    const viz::ViewportTransform& t = pane.viewport.transform();
    char buffer[256];
    const std::optional<viz::Vec2> cursor = pane.viewport.cursorImagePosition();
    if (cursor) {
        std::snprintf(buffer, sizeof(buffer), "x=%d  y=%d", static_cast<int>(std::floor(cursor->x)),
                      static_cast<int>(std::floor(cursor->y)));
    } else {
        std::snprintf(buffer, sizeof(buffer), "x=-  y=-");
    }
    ImGui::TextUnformatted(buffer);
    ImGui::SameLine(0, 18);
    ImGui::Text("zoom %.0f%%", t.zoom() * 100.0);
    ImGui::SameLine(0, 18);
    ImGui::Text("image %d x %d px", t.imageWidth(), t.imageHeight());
    ImGui::SameLine(0, 18);
    ImGui::Text("viewport %.0f x %.0f", t.viewportSize().x, t.viewportSize().y);
    ImGui::SameLine(0, 18);
    ImGui::TextDisabled("%s", viz::toString(context.view.mode));
    if (!notice_.empty()) {
        ImGui::SameLine(0, 18);
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.6f, 1.0f), "%s", notice_.c_str());
    }
}

void ImageComparisonPanel::draw(GuiContext& context) {
    ViewState& vs = context.view;
    const InspectionView& view = context.controller.view();

    if (boxes_generation_ != view.generation) {
        boxes_.clear();
        for (const DefectRow& d : view.defects) {
            boxes_.push_back({{static_cast<double>(d.x), static_cast<double>(d.y), static_cast<double>(d.width),
                               static_cast<double>(d.height)},
                              d.index,
                              d.area_px});
        }
        boxes_generation_ = view.generation;
        notice_.clear();
    }

    drawToolStrip(context);
    if (vs.request_clear_roi) {
        roi_.reset();
        vs.request_clear_roi = false;
    }
    if (vs.request_export_roi) {
        exportRoi(context);
        vs.request_export_roi = false;
    }

    active_panes_.clear();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float status_height = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
    avail.y -= status_height;
    if (avail.x < 16.0f || avail.y < 16.0f) return;

    const viz::ViewMode mode = vs.mode;
    const bool side_by_side = mode == viz::ViewMode::SideBySide;
    std::string message;

    if (!view.hasResult()) {
        message = "No inspection loaded.\nSelect a reference and an inspected image and press Run Inspection,\nor load a stored inspection from the History page.";
    } else {
        message = unavailableReason(mode);
    }

    const float gap = 6.0f;
    if (message.empty()) {
        if (side_by_side) {
            const float half = std::floor((avail.x - gap) * 0.5f);
            left_.rect = {origin.x, origin.y, half, avail.y};
            right_.rect = {origin.x + half + gap, origin.y, half, avail.y};
            left_.texture_a = current_[Reference];
            right_.texture_a = current_[Inspected];
            left_.blend = right_.blend = false;
            left_.title = "Reference";
            right_.title = "Inspected";
            active_panes_ = {&left_, &right_};
        } else if (mode == viz::ViewMode::Blend) {
            if (current_[Reference]->width() != current_[Inspected]->width() ||
                current_[Reference]->height() != current_[Inspected]->height()) {
                message = "Blend needs reference and inspected images of identical size.";
            } else {
                main_.rect = {origin.x, origin.y, avail.x, avail.y};
                main_.texture_a = current_[Reference];
                main_.texture_b = current_[Inspected];
                main_.blend = true;
                active_panes_ = {&main_};
            }
        } else {
            main_.rect = {origin.x, origin.y, avail.x, avail.y};
            main_.texture_a = textureFor(mode);
            main_.texture_b.reset();
            main_.blend = false;
            active_panes_ = {&main_};
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active_panes_.empty()) {
        dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(15, 18, 22, 255));
        const ImVec2 size = ImGui::CalcTextSize(message.c_str());
        dl->AddText(ImVec2(origin.x + (avail.x - size.x) * 0.5f, origin.y + (avail.y - size.y) * 0.5f),
                    IM_COL32(150, 160, 170, 255), message.c_str());
        ImGui::Dummy(ImVec2(avail.x, avail.y));
        return;
    }

    for (Pane* pane : active_panes_) {
        pane->viewport.transform().setImageSize(pane->texture_a->width(), pane->texture_a->height());
        pane->viewport.transform().setViewport({pane->rect.x, pane->rect.y}, {pane->rect.w, pane->rect.h});
        if (roi_ != pane->viewport.roi() && !pane->viewport.roiDragActive()) {
            if (roi_) pane->viewport.setRoi(*roi_);
            else pane->viewport.clearRoi();
        }
    }
    context_blend_ = vs.blend;

    // One-shot view commands.
    if (vs.request_fit) {
        for (Pane* pane : active_panes_) pane->viewport.transform().fit();
        vs.request_fit = false;
    }
    if (vs.request_actual) {
        for (Pane* pane : active_panes_) pane->viewport.transform().actualSize();
        vs.request_actual = false;
    }
    if (vs.request_center_selected) {
        vs.request_center_selected = false;
        if (vs.selected_defect) {
            for (const viz::DefectBox& b : boxes_) {
                if (b.index != *vs.selected_defect) continue;
                for (Pane* pane : active_panes_) {
                    viz::ViewportTransform& t = pane->viewport.transform();
                    if (t.zoom() < 2.0) t.setZoom(2.0);
                    t.centerOn({b.image_rect.x + b.image_rect.w * 0.5, b.image_rect.y + b.image_rect.h * 0.5});
                }
                break;
            }
        }
    }

    // Input: one invisible button covers the whole area, so wheel/drag are captured by the viewport.
    ImGui::InvisibleButton("##image_viewport", ImVec2(avail.x, avail.y),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (active_panes_.size() == 1) {
        active_pane_index_ = 0;
    } else if (!active && hovered) {
        const float mx = ImGui::GetIO().MousePos.x;
        active_pane_index_ = mx < static_cast<float>(right_.rect.x) ? 0 : 1;
    }
    Pane& target = *active_panes_[std::min(active_pane_index_, active_panes_.size() - 1)];
    if (hovered || active) handleInput(context, target, side_by_side);

    for (Pane* pane : active_panes_) {
        buildOverlays(context, *pane);
        dl->AddRect(toIm({pane->rect.x, pane->rect.y}), toIm({pane->rect.right(), pane->rect.bottom()}),
                    IM_COL32(60, 70, 82, 255));
        if (side_by_side) {
            dl->AddText(ImVec2(static_cast<float>(pane->rect.x) + 8.0f, static_cast<float>(pane->rect.y) + 6.0f),
                        IM_COL32(230, 235, 240, 230), pane->title.c_str());
        }
    }

    drawStatusLine(context, target);
}

void ImageComparisonPanel::renderGl(int framebuffer_height, double scale_x, double scale_y) {
    if (active_panes_.empty()) return;
    Stopwatch cpu_timer;
    gpu_timer_.begin();
    for (Pane* pane : active_panes_) {
        const viz::RectI vp = viz::toGlViewportRect(pane->rect, scale_x, scale_y, framebuffer_height);
        image_renderer_.beginViewport(vp, config_.viewport_background);
        viz::ViewportTransform& t = pane->viewport.transform();
        if (pane->blend) {
            image_renderer_.drawBlend(*pane->texture_a, *pane->texture_b, t, context_blend_);
        } else {
            image_renderer_.drawImage(*pane->texture_a, t);
        }
        overlay_renderer_.render(pane->geometry, t.viewportSize());
        image_renderer_.endViewport();
    }
    gpu_timer_.end();
    metrics_.gl_submit_ms.add(cpu_timer.elapsedMilliseconds());
    if (gpu_timer_.samples() != gpu_samples_seen_) {
        gpu_samples_seen_ = gpu_timer_.samples();
        metrics_.gpu_render_ms.add(gpu_timer_.lastMs());
    }
    metrics_.texture_bytes = cache_.bytes();
    metrics_.texture_count = cache_.size();
    metrics_.cache_hits = cache_.stats().hits;
    metrics_.cache_misses = cache_.stats().misses;
    metrics_.cache_evictions = cache_.stats().evictions;
    active_panes_.clear();
}

}  // namespace optinspect::gui
