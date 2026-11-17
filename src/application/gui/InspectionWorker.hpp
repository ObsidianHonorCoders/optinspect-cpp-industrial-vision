#pragma once

#include "AsyncTask.hpp"
#include "GuiTypes.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>

namespace optinspect::gui {

struct InspectionRequest {
    std::string reference_path;
    std::string inspected_path;
    std::string config_path;
    std::string output_dir;

    bool persist = false;  ///< Store the finished inspection through the Phase 2 services.
    std::string database_path;
    std::optional<std::int64_t> product_id;
    std::optional<std::string> serial_number;
    std::optional<std::string> notes;

    /// Texture cache keys that already exist on the GPU; matching images are not decoded again.
    std::unordered_set<std::string> cached_keys;
};

/// Phase of a running job. Only phases the code can really observe are reported:
/// the pipeline stages come from the StageObserver of the real pipeline, so no
/// percentages are invented.
enum class WorkerPhase { Idle, Loading, Aligning, Detecting, Deciding, WritingReports, Persisting, Completed, Failed };

const char* toString(WorkerPhase phase);

struct InspectionJobResult {
    bool success = false;
    std::string error;                ///< Inspection error (nothing was produced).
    std::string persistence_error;    ///< Set when the inspection ran but storing it failed.
    InspectionView view;              ///< Valid when success.
    double worker_ms = 0.0;           ///< Wall time of the whole job on the worker thread.
};

/// Runs the EXISTING inspection pipeline (runInspection -> InspectionPipeline)
/// and the Phase 2 persistence (InspectionRecorder) on one owned worker thread.
///
/// Threading contract
///  - submit()/poll()/phase() are called from the GUI thread; the job runs on the worker.
///  - The job never touches ImGui or OpenGL. It receives its input by value and
///    returns an immutable result (pixels are shared_ptr<const cv::Mat>).
///  - Progress is a std::atomic<WorkerPhase>; the result is handed over under a mutex.
///  - The destructor requests cancellation and joins: a running job stops at its
///    next stage boundary (the pipeline itself is not interruptible mid-stage), so
///    shutdown waits at most for one stage. No detached threads.
class InspectionWorker {
public:
    InspectionWorker() = default;
    ~InspectionWorker();
    InspectionWorker(const InspectionWorker&) = delete;
    InspectionWorker& operator=(const InspectionWorker&) = delete;

    /// Starts a job. Returns false (and does nothing) while another job is running.
    bool submit(InspectionRequest request);

    bool busy() const { return task_.busy(); }
    WorkerPhase phase() const { return phase_.load(); }

    /// Finished job, once; nullopt while running or if nothing is pending.
    std::optional<InspectionJobResult> poll();

    /// Cooperative cancellation (also used by the destructor).
    void requestCancel() { cancel_ = true; }

private:
    InspectionJobResult execute(const InspectionRequest& request);

    AsyncTask<InspectionJobResult> task_;
    std::atomic<WorkerPhase> phase_{WorkerPhase::Idle};
    std::atomic<bool> cancel_{false};
};

}  // namespace optinspect::gui
