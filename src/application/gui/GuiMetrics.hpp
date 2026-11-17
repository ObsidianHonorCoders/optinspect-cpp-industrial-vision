#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <limits>
#include <string>

namespace optinspect::gui {

/// Running statistics over the last `capacity` samples (ms values). Cheap enough to update every frame.
class RollingStat {
public:
    explicit RollingStat(std::size_t capacity = 240) : capacity_(capacity) {}

    void add(double value) {
        samples_.push_back(value);
        if (samples_.size() > capacity_) samples_.pop_front();
        ++total_count_;
        max_all_ = std::max(max_all_, value);
    }

    std::size_t count() const { return samples_.size(); }
    std::size_t totalCount() const { return total_count_; }
    bool empty() const { return samples_.empty(); }
    double last() const { return samples_.empty() ? 0.0 : samples_.back(); }
    double average() const {
        if (samples_.empty()) return 0.0;
        double sum = 0.0;
        for (double v : samples_) sum += v;
        return sum / static_cast<double>(samples_.size());
    }
    double maxRecent() const {
        return samples_.empty() ? 0.0 : *std::max_element(samples_.begin(), samples_.end());
    }
    double maxEver() const { return total_count_ == 0 ? 0.0 : max_all_; }
    const std::deque<double>& samples() const { return samples_; }
    void clear() {
        samples_.clear();
        total_count_ = 0;
        max_all_ = std::numeric_limits<double>::lowest();
    }

private:
    std::size_t capacity_;
    std::deque<double> samples_;
    std::size_t total_count_ = 0;
    double max_all_ = std::numeric_limits<double>::lowest();
};

/// One timed event with a label (image load, texture upload, DB query, inspection job).
struct TimedEvent {
    std::string label;
    double ms = 0.0;
    std::size_t bytes = 0;  ///< Size involved (e.g. texture bytes), 0 if not applicable.
};

/// Measurements shown in the performance panel and written by `--benchmark`.
/// CPU time, GPU upload time, GPU render time and UI frame time are kept apart.
struct PerformanceMetrics {
    // One-off startup figures.
    double startup_ms = 0.0;        ///< Process start of main() to first frame presented.
    double gl_init_ms = 0.0;        ///< GLFW + window + context + GLAD.
    double renderer_init_ms = 0.0;  ///< Shader compilation, VAO/VBO creation, ImGui backend init.

    // Per-frame figures.
    RollingStat frame_ms{240};      ///< Whole loop iteration including waiting for vsync/swap.
    RollingStat cpu_frame_ms{240};  ///< Loop iteration without the swap/vsync wait.
    RollingStat ui_build_ms{240};   ///< Building the ImGui UI (CPU).
    RollingStat gl_submit_ms{240};  ///< CPU time spent issuing image/overlay GL commands.
    RollingStat gpu_render_ms{240}; ///< GPU time of image + overlay drawing (timer query).
    RollingStat imgui_render_ms{240};

    // Events.
    RollingStat texture_upload_ms{64};
    RollingStat image_load_ms{64};
    RollingStat db_query_ms{64};
    RollingStat inspection_worker_ms{16};
    TimedEvent last_upload;
    TimedEvent last_image_load;

    // UI responsiveness while the worker runs.
    RollingStat frame_ms_during_inspection{600};

    // Texture memory.
    std::size_t texture_bytes = 0;
    std::size_t texture_count = 0;
    std::size_t cache_hits = 0;
    std::size_t cache_misses = 0;
    std::size_t cache_evictions = 0;
    std::size_t images_skipped_cached = 0;  ///< Decodes avoided because a texture was cached.
    std::size_t texture_allocations = 0;    ///< glTexImage2D calls.
    std::size_t texture_updates = 0;        ///< glTexSubImage2D calls.
};

}  // namespace optinspect::gui
