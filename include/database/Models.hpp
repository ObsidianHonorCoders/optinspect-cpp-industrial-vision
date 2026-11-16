#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace optinspect::database {

/// Plain data records exchanged with the repositories. They deliberately
/// contain no OpenCV types: the database layer is independent of the image
/// processing engine and receives already-extracted values.

enum class InspectionStatus { PASS, FAIL, REVIEW };
enum class AlignmentStatus { SUCCEEDED, FAILED };
enum class ImageType { REFERENCE, INSPECTED, ANNOTATED, DIFFERENCE };

std::string toString(InspectionStatus status);
std::string toString(AlignmentStatus status);
std::string toString(ImageType type);

/// Parsers throw std::invalid_argument for unknown text.
InspectionStatus parseInspectionStatus(const std::string& text);
AlignmentStatus parseAlignmentStatus(const std::string& text);
ImageType parseImageType(const std::string& text);

/// A PCB product/model (not an individual board or inspection event).
struct Product {
    std::int64_t id = 0;  ///< Assigned by the database on create.
    std::string code;     ///< Unique business key, e.g. "PCB-001".
    std::string name;
    std::optional<std::string> model;
    std::optional<std::string> revision;
    std::optional<std::string> description;
    std::string created_at;  ///< UTC ISO-8601, assigned on create.
};

/// One inspection execution. Image file paths live in InspectionImage rows;
/// `configuration_snapshot` is the JSON text of the configuration used.
struct InspectionRecord {
    std::int64_t id = 0;
    /// Identifier assigned by the Phase 1 CLI (e.g. "20261114-213626-00041000");
    /// ties the row to the report/image file names. Unique when present.
    std::optional<std::string> external_id;
    std::optional<std::int64_t> product_id;
    std::optional<std::string> serial_number;
    std::string inspection_timestamp;  ///< UTC ISO-8601, e.g. 2026-11-14T20:36:26Z.
    InspectionStatus status = InspectionStatus::REVIEW;
    std::optional<std::string> decision_reason;
    std::optional<AlignmentStatus> alignment_status;
    std::optional<double> alignment_confidence;
    std::optional<std::string> report_path;
    int detected_defect_count = 0;  ///< Maintained by triggers; ignored on create/update.
    std::optional<double> processing_time_ms;
    std::optional<std::string> configuration_snapshot;
    std::optional<std::int64_t> configuration_id;  ///< Output only.
    std::optional<std::string> notes;
    std::string created_at;
};

/// One suspicious region measured by the Phase 1 detector. These are
/// geometric measurements only: Phase 1 does not classify defect types, so
/// there is deliberately no defect_type or severity column.
struct DefectRecord {
    std::int64_t id = 0;
    std::int64_t inspection_id = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    double area = 0.0;
    std::optional<double> perimeter;
    std::optional<double> centroid_x;
    std::optional<double> centroid_y;
    std::optional<double> aspect_ratio;
    /// Phase 1 rule-based region score in [0,1]; not a classification confidence.
    std::optional<double> confidence;
    std::optional<std::string> review_note;
    std::string created_at;
};

/// Reference to a file on disk; the image itself is never stored in SQLite.
struct InspectionImage {
    std::int64_t id = 0;
    std::int64_t inspection_id = 0;
    ImageType type = ImageType::REFERENCE;
    std::string file_path;
    std::optional<int> width;
    std::optional<int> height;
    std::string created_at;
};

/// Criteria shared by the history queries and the analytics queries.
/// `from` is inclusive; `to` is inclusive when given as a date
/// ("YYYY-MM-DD") and exclusive when given as a full timestamp.
struct InspectionFilter {
    std::optional<std::int64_t> product_id;
    std::optional<std::string> serial_number;
    std::optional<InspectionStatus> status;
    std::optional<std::string> from;
    std::optional<std::string> to;
    std::optional<int> limit;   ///< Honoured by list(); ignored by analytics.
    int offset = 0;
};

struct ProductFilter {
    std::optional<std::string> search;  ///< Case-insensitive substring of code or name.
    std::optional<int> limit;
};

struct DefectFilter {
    std::optional<std::int64_t> inspection_id;
    std::optional<double> min_area;
    std::optional<double> max_area;
    std::optional<double> min_confidence;
    std::optional<double> max_confidence;
    std::optional<int> limit;
};

}  // namespace optinspect::database
