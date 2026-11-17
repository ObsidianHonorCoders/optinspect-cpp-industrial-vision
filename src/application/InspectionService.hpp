#pragma once

#include "InspectionPipeline.hpp"
#include "InspectionRecorder.hpp"

#include "common/Config.hpp"
#include "common/Timing.hpp"
#include "detection/InspectionResult.hpp"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace optinspect {

/// Everything one inspection run produced, minus the pixel data (which lives
/// in the files named here).
struct InspectionOutcome {
    InspectionResult result;
    std::string annotated_path;
    std::string diff_path;
    std::string report_path;
    std::vector<StageTiming> stages;
    double total_ms = 0.0;
    cv::Size reference_size;
    cv::Size inspected_size;
    cv::Size annotated_size;
    cv::Size diff_size;
};

/// Inspection id in the form YYYYMMDD-HHMMSS (local time), as used by the CLI.
std::string generateInspectionId();

/// Absolute, normalized form of a path so stored references remain valid
/// regardless of the working directory the application is later started from.
std::string absolutePath(const std::string& path);

/// Runs one reference/inspected inspection and writes its annotated image,
/// difference image and JSON report into `output_dir`, all named after
/// `inspection_id`. Shared by the CLI and the GUI so both produce identical
/// outputs. `observer` (optional) is told "loading", "alignment", "detection",
/// "decision" and "writing" just before each phase starts.
InspectionOutcome runInspection(const InspectionConfig& config, const std::string& reference_path,
                                const std::string& inspected_path, const std::string& output_dir,
                                const std::string& inspection_id,
                                const StageObserver& observer = {});

/// Builds the artifact description persisted with an inspection. Paths are made absolute.
InspectionArtifacts makeArtifacts(const std::string& inspection_id,
                                  const std::string& reference_path,
                                  const std::string& inspected_path,
                                  const InspectionOutcome& outcome);

}  // namespace optinspect
