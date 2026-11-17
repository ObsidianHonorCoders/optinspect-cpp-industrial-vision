#pragma once

#include "database/Models.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace optinspect::gui {

/// One measured region as shown in the GUI. The fields mirror the Phase 1
/// DefectMeasurement (live inspections) / the stored DefectRecord (history).
/// Optional values are empty when the source did not store them.
struct DefectRow {
    int index = 0;  ///< 1-based position in the list; the label shown in the viewport.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    double area_px = 0.0;
    std::optional<double> perimeter_px;
    std::optional<double> centroid_x;
    std::optional<double> centroid_y;
    std::optional<double> aspect_ratio;
    std::optional<double> confidence;  ///< Phase 1 rule-based region score, not a classification.
    std::optional<std::string> review_note;
};

struct StageTimingRow {
    std::string name;
    double ms = 0.0;
};

/// An image that was (or was not) loaded for display. Pixels are immutable once
/// published to the GUI thread (shared_ptr<const>), so no locking is needed.
struct ImageSlot {
    std::string path;
    /// Texture cache key: "<absolute path>|<mtime>|<size>". Empty when the file is missing.
    std::string cache_key;
    std::shared_ptr<const cv::Mat> pixels;  ///< Null when skipped (already cached) or on error.
    bool already_cached = false;            ///< Decoding was skipped because a texture exists.
    std::string error;                      ///< Non-empty when the file was missing or undecodable.
    double load_ms = 0.0;

    bool available() const { return error.empty() && !cache_key.empty(); }
};

struct DisplayImages {
    ImageSlot reference;
    ImageSlot inspected;
    ImageSlot annotated;
    ImageSlot difference;
};

enum class ResultSource { None, LiveInspection, History };

/// Everything the workspace shows about one inspection (live or historical).
struct InspectionView {
    ResultSource source = ResultSource::None;
    std::uint64_t generation = 0;  ///< Increases for every newly installed view.

    std::string external_id;
    std::string decision;  ///< "PASS" / "FAIL" / "REVIEW" as decided (and stored) by Phase 1 rules.
    std::string decision_reason;
    std::optional<bool> alignment_succeeded;
    std::optional<double> alignment_confidence;
    std::optional<std::int64_t> db_id;
    std::optional<std::int64_t> product_id;
    std::optional<std::string> product_label;
    std::optional<std::string> serial_number;
    std::string timestamp;
    std::optional<double> processing_time_ms;
    std::string report_path;

    std::vector<DefectRow> defects;
    std::vector<StageTimingRow> stages;
    DisplayImages images;

    std::vector<std::string> warnings;  ///< e.g. missing historical image files.

    bool hasResult() const { return source != ResultSource::None; }
};

}  // namespace optinspect::gui
