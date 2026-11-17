#pragma once

#include "GuiState.hpp"

#include "visualization/DefectOverlayRenderer.hpp"
#include "visualization/GpuTimer.hpp"
#include "visualization/ImageRenderer.hpp"
#include "visualization/ImageViewport.hpp"
#include "visualization/TextureCache.hpp"
#include "visualization/VisualizationConfig.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace optinspect::gui {

/// Central OpenGL viewport of the workspace. It owns every GPU resource used for
/// image display (renderers, textures, GPU timer) and therefore must be created
/// and destroyed while the GL context is current, on the GL thread.
///
/// Frame protocol (GL thread only):
///   1. syncTextures()  - upload images of a newly installed InspectionView (cache aware)
///   2. draw()          - inside an ImGui window: reserves the viewport rectangle, handles
///                        mouse input, ROI, defect picking, text overlays
///   3. ImGui::Render()
///   4. renderGl()      - draws the images and overlay primitives into the reserved
///                        rectangle(s) of the default framebuffer
///   5. ImGui_ImplOpenGL3_RenderDrawData() - the UI is drawn on top
class ImageComparisonPanel {
public:
    ImageComparisonPanel(const viz::VisualizationConfig& config, const std::filesystem::path& shader_dir,
                         PerformanceMetrics& metrics);

    void syncTextures(const InspectionView& view);
    void draw(GuiContext& context);
    void renderGl(int framebuffer_height, double scale_x, double scale_y);

    /// Texture cache keys currently on the GPU (given to the background loaders).
    std::unordered_set<std::string> cachedKeys() const;

    /// Measure uploads including the GPU completion (glFinish). Only for benchmarks.
    void setSynchronousUploadTiming(bool enabled) { sync_upload_timing_ = enabled; }

    const viz::CacheStats& cacheStats() const { return cache_.stats(); }
    std::size_t textureBytes() const { return cache_.bytes(); }
    std::size_t textureCount() const { return cache_.size(); }
    double lastGpuMs() const { return gpu_timer_.lastMs(); }

    /// Benchmark hooks: drive the main viewport programmatically.
    viz::ImageViewport& mainViewport() { return main_.viewport; }
    /// Screen centre of the main viewport rectangle (for scripted input).
    viz::Vec2 mainViewportCenter() const { return {main_.rect.x + main_.rect.w * 0.5, main_.rect.y + main_.rect.h * 0.5}; }
    /// Scripted use (screenshots): preset the region of interest, in image pixels.
    void presetRoi(const viz::RectI& roi) { roi_ = roi; }

    static constexpr int kRoleCount = 4;
    enum Role { Reference = 0, Inspected = 1, Annotated = 2, Difference = 3 };

private:
    using TexturePtr = std::shared_ptr<viz::Texture2D>;

    struct Pane {
        viz::ImageViewport viewport;
        viz::RectD rect;                 ///< Screen rectangle (ImGui coordinates).
        TexturePtr texture_a;
        TexturePtr texture_b;            ///< Second texture for Blend mode.
        bool blend = false;
        viz::OverlayGeometry geometry;
        std::string title;
        explicit Pane(const viz::VisualizationConfig& config) : viewport(config) {}
    };

    void uploadSlot(Role role, const ImageSlot& slot);
    TexturePtr textureFor(viz::ViewMode mode) const;
    std::string unavailableReason(viz::ViewMode mode) const;
    void drawToolStrip(GuiContext& context);
    void handleInput(GuiContext& context, Pane& pane, bool side_by_side);
    void buildOverlays(GuiContext& context, Pane& pane);
    void drawStatusLine(const GuiContext& context, const Pane& pane);
    void exportRoi(GuiContext& context);

    viz::VisualizationConfig config_;
    PerformanceMetrics& metrics_;
    viz::ImageRenderer image_renderer_;
    viz::DefectOverlayRenderer overlay_renderer_;
    viz::GpuTimer gpu_timer_;
    viz::BoundedLruCache<viz::Texture2D> cache_;

    std::array<TexturePtr, kRoleCount> current_;
    std::array<std::string, kRoleCount> role_error_;
    std::uint64_t synced_generation_ = 0;
    std::uint64_t boxes_generation_ = 0;
    std::vector<viz::DefectBox> boxes_;
    std::string external_id_;
    std::string annotated_dir_;
    bool sync_upload_timing_ = false;

    Pane main_;
    Pane left_;
    Pane right_;
    std::vector<Pane*> active_panes_;
    std::optional<viz::RectI> roi_;
    std::string notice_;        ///< Transient message shown in the status line.
    std::string empty_message_; ///< Shown instead of an image when nothing can be drawn.
    std::size_t active_pane_index_ = 0;
    float context_blend_ = 0.5f;
    std::uint64_t gpu_samples_seen_ = 0;
};

}  // namespace optinspect::gui
