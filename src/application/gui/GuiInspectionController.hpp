#pragma once

#include "AsyncTask.hpp"
#include "GuiMetrics.hpp"
#include "GuiTypes.hpp"
#include "HistoryService.hpp"
#include "InspectionWorker.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace optinspect::gui {

/// Values edited in the inspection panel.
struct InspectionSettings {
    std::string reference_path;
    std::string inspected_path;
    std::string config_path = "config/inspection_config.json";
    std::string output_dir = "results";
    bool persist = false;
    std::string database_path = "data/optinspect.db";
    std::string product_id_text;  ///< Empty = no product.
    std::string serial_number;
    std::string notes;
};

/// Filter edited in the history / analytics panels.
struct FilterSettings {
    int status = 0;  ///< 0 = any, 1 = PASS, 2 = FAIL, 3 = REVIEW.
    std::string product_id_text;
    std::string serial_number;
    std::string date_from;  ///< "YYYY-MM-DD" or empty.
    std::string date_to;
    std::string id_search;
    int limit = 200;
    int granularity = 0;  ///< 0 = day, 1 = week, 2 = month (analytics only).
};

enum class LogLevel { Info, Warning, Error };

struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string text;
};

/// Application-level controller of the GUI: owns the inspection worker and the
/// background history/analytics jobs, validates user input, and holds the data
/// the panels display. It contains no ImGui or OpenGL code and is driven by
/// update() on the GUI thread, which makes the whole inspection / history /
/// analytics workflow testable without a window.
///
/// All results of background jobs are installed in update() (GUI thread), so
/// panels never observe partially written data.
class GuiInspectionController {
public:
    using CachedKeysProvider = std::function<std::unordered_set<std::string>()>;

    /// `cached_keys` tells the loaders which texture cache keys already exist on
    /// the GPU (so those images are not decoded again). `metrics` is optional.
    explicit GuiInspectionController(CachedKeysProvider cached_keys = {}, PerformanceMetrics* metrics = nullptr);

    InspectionSettings& settings() { return settings_; }
    const InspectionSettings& settings() const { return settings_; }
    FilterSettings& historyFilter() { return history_filter_; }
    FilterSettings& analyticsFilter() { return analytics_filter_; }

    /// Empty string when the settings are valid, otherwise the first problem found.
    std::string validateSettings() const;

    /// Validates and submits an inspection to the worker. Returns false (with a log message) otherwise.
    bool runInspection();
    bool inspectionRunning() const { return worker_.busy(); }
    WorkerPhase workerPhase() const { return worker_.phase(); }

    /// Asynchronous database jobs. They only start when settings.database_path is set.
    bool refreshHistory();
    bool refreshAnalytics();
    /// Loads a stored inspection (record, defects, image references, images) as the current view.
    bool selectInspection(std::int64_t inspection_id);
    bool selectionLoading() const { return details_task_.busy(); }

    /// Polls every background job and installs finished results. Call once per frame.
    void update();

    /// True while any worker/database job is running.
    bool anyBusy() const;

    const InspectionView& view() const { return view_; }
    /// Increments whenever `view()` was replaced (the viewport re-uploads textures then).
    std::uint64_t viewGeneration() const { return view_.generation; }

    const HistoryResult* history() const { return history_ ? &*history_ : nullptr; }
    const std::string& historyError() const { return history_error_; }
    bool historyBusy() const { return history_task_.busy(); }
    std::optional<std::int64_t> selectedHistoryId() const { return selected_history_id_; }

    const AnalyticsSnapshot* analytics() const { return analytics_ ? &*analytics_ : nullptr; }
    const std::string& analyticsError() const { return analytics_error_; }
    bool analyticsBusy() const { return analytics_task_.busy(); }

    const std::string& lastInspectionError() const { return last_inspection_error_; }
    const std::string& lastPersistenceError() const { return last_persistence_error_; }

    const std::vector<LogEntry>& log() const { return log_; }
    void addLog(LogLevel level, const std::string& text);

    /// Builds the database filter from the panel state. Throws std::invalid_argument for a bad product id.
    static database::InspectionFilter toDatabaseFilter(const FilterSettings& settings, bool apply_limit);

private:
    std::unordered_set<std::string> cachedKeys() const;
    void installView(InspectionView view);

    InspectionSettings settings_;
    FilterSettings history_filter_;
    FilterSettings analytics_filter_;
    CachedKeysProvider cached_keys_provider_;
    PerformanceMetrics* metrics_ = nullptr;

    InspectionWorker worker_;
    AsyncTask<HistoryResult> history_task_;
    AsyncTask<AnalyticsSnapshot> analytics_task_;
    AsyncTask<InspectionView> details_task_;

    InspectionView view_;
    std::optional<HistoryResult> history_;
    std::string history_error_;
    std::optional<AnalyticsSnapshot> analytics_;
    std::string analytics_error_;
    std::optional<std::int64_t> selected_history_id_;
    std::string last_inspection_error_;
    std::string last_persistence_error_;
    std::vector<LogEntry> log_;
    std::uint64_t next_generation_ = 1;
};

}  // namespace optinspect::gui
