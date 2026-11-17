#include "InspectionWorker.hpp"

#include "ImageLoader.hpp"
#include "InspectionRecorder.hpp"
#include "InspectionService.hpp"

#include "common/Config.hpp"
#include "common/Errors.hpp"
#include "common/Timing.hpp"
#include "database/TimeUtil.hpp"

#include <filesystem>

namespace optinspect::gui {

namespace fs = std::filesystem;

namespace {

struct JobCancelled {};

DefectRow toRow(int index, const DefectMeasurement& d) {
    DefectRow r;
    r.index = index;
    r.x = d.bounding_box.x;
    r.y = d.bounding_box.y;
    r.width = d.width;
    r.height = d.height;
    r.area_px = d.area_px;
    r.perimeter_px = d.perimeter_px;
    r.centroid_x = d.centroid.x;
    r.centroid_y = d.centroid.y;
    r.aspect_ratio = d.aspect_ratio;
    r.confidence = d.confidence;
    return r;
}

/// Inspection id that does not collide with files already in the output folder
/// (generateInspectionId has one-second resolution).
std::string uniqueInspectionId(const std::string& output_dir) {
    const std::string base = generateInspectionId();
    std::string id = base;
    for (int n = 2; fs::exists(fs::path(output_dir) / (id + "_report.json")); ++n) {
        id = base + "-" + std::to_string(n);
    }
    return id;
}

}  // namespace

const char* toString(WorkerPhase phase) {
    switch (phase) {
        case WorkerPhase::Idle: return "Idle";
        case WorkerPhase::Loading: return "Loading images";
        case WorkerPhase::Aligning: return "Aligning images";
        case WorkerPhase::Detecting: return "Comparing images and detecting regions";
        case WorkerPhase::Deciding: return "Deciding PASS / FAIL / REVIEW";
        case WorkerPhase::WritingReports: return "Generating reports and images";
        case WorkerPhase::Persisting: return "Persisting results";
        case WorkerPhase::Completed: return "Completed";
        case WorkerPhase::Failed: return "Failed";
    }
    return "?";
}

InspectionWorker::~InspectionWorker() {
    requestCancel();
    task_.join();
}

bool InspectionWorker::submit(InspectionRequest request) {
    if (task_.busy()) return false;
    cancel_ = false;
    phase_ = WorkerPhase::Loading;
    return task_.start([this, request = std::move(request)]() { return execute(request); });
}

std::optional<InspectionJobResult> InspectionWorker::poll() {
    std::optional<TaskResult<InspectionJobResult>> finished = task_.poll();
    if (!finished) return std::nullopt;

    if (finished->ok()) return std::move(finished->value);

    // execute() converts every expected failure itself; reaching here means an
    // exception escaped (e.g. cancellation or an unexpected error type).
    InspectionJobResult failed;
    failed.error = finished->error.empty() ? "The inspection was cancelled." : finished->error;
    failed.worker_ms = finished->elapsed_ms;
    phase_ = WorkerPhase::Failed;
    return failed;
}

InspectionJobResult InspectionWorker::execute(const InspectionRequest& request) {
    Stopwatch job_timer;
    InspectionJobResult out;

    const auto fail = [&](const std::string& message) {
        out.success = false;
        out.error = message;
        out.worker_ms = job_timer.elapsedMilliseconds();
        phase_ = WorkerPhase::Failed;
        return out;
    };

    try {
        const InspectionConfig config = InspectionConfig::loadFromFile(request.config_path);
        const std::string output_dir = request.output_dir.empty() ? config.default_output_dir : request.output_dir;
        fs::create_directories(output_dir);
        const std::string inspection_id = uniqueInspectionId(output_dir);

        const StageObserver observer = [this](const std::string& stage) {
            if (cancel_) throw JobCancelled{};
            if (stage == "loading") phase_ = WorkerPhase::Loading;
            else if (stage == "alignment") phase_ = WorkerPhase::Aligning;
            else if (stage == "detection") phase_ = WorkerPhase::Detecting;
            else if (stage == "decision") phase_ = WorkerPhase::Deciding;
            else if (stage == "writing") phase_ = WorkerPhase::WritingReports;
        };

        const InspectionOutcome outcome = runInspection(config, request.reference_path, request.inspected_path,
                                                        output_dir, inspection_id, observer);

        InspectionView& view = out.view;
        view.source = ResultSource::LiveInspection;
        view.external_id = inspection_id;
        view.decision = toString(outcome.result.decision);
        view.decision_reason = outcome.result.decision_reason;
        view.alignment_succeeded = outcome.result.alignment_succeeded;
        view.alignment_confidence = outcome.result.alignment_confidence;
        view.product_id = request.product_id;
        view.serial_number = request.serial_number;
        view.timestamp = database::nowUtcTimestamp();
        view.processing_time_ms = outcome.total_ms;
        view.report_path = outcome.report_path;
        int index = 1;
        for (const DefectMeasurement& d : outcome.result.defects) view.defects.push_back(toRow(index++, d));
        for (const StageTiming& t : outcome.stages) view.stages.push_back({t.stage_name, t.milliseconds});

        // Persistence uses the same artifacts/recorder as the CLI. A failure here
        // never changes the inspection result; it is reported next to it.
        if (request.persist) {
            if (cancel_) throw JobCancelled{};
            phase_ = WorkerPhase::Persisting;
            try {
                InspectionRecorder recorder(request.database_path, config.database.auto_initialize);
                if (request.product_id && !recorder.productExists(*request.product_id)) {
                    throw OptInspectError("Product " + std::to_string(*request.product_id) +
                                          " does not exist in " + request.database_path);
                }
                PersistenceOptions options;
                options.product_id = request.product_id;
                options.serial_number = request.serial_number;
                options.notes = request.notes;
                const InspectionArtifacts artifacts = makeArtifacts(inspection_id, request.reference_path,
                                                                    request.inspected_path, outcome);
                view.db_id = recorder.record(buildPersistenceRequest(outcome.result, config, artifacts, options,
                                                                      view.timestamp));
            } catch (const std::exception& e) {
                out.persistence_error = e.what();
            }
        }

        // Display images are decoded here, on the worker thread, not in the render loop.
        view.images.reference = loadImageSlot(request.reference_path, request.cached_keys);
        view.images.inspected = loadImageSlot(request.inspected_path, request.cached_keys);
        view.images.annotated = loadImageSlot(outcome.annotated_path, request.cached_keys);
        if (!outcome.diff_path.empty()) {
            view.images.difference = loadImageSlot(outcome.diff_path, request.cached_keys);
        } else {
            view.images.difference.error = "No difference image: alignment failed, so no comparison was made.";
        }

        out.success = true;
        out.worker_ms = job_timer.elapsedMilliseconds();
        phase_ = WorkerPhase::Completed;
        return out;
    } catch (const JobCancelled&) {
        return fail("The inspection was cancelled.");
    } catch (const cv::Exception& e) {
        return fail(std::string("OpenCV error: ") + e.what());
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

}  // namespace optinspect::gui
