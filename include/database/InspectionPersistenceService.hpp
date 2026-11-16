#pragma once

#include "database/DatabaseConnection.hpp"
#include "database/Models.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace optinspect::database {

/// Everything that makes up one recorded inspection. `inspection` supplies
/// the header row; `defects` and `images` are written with it. The `id` and
/// `inspection_id` fields inside the nested records are assigned by the service.
struct InspectionPersistenceRequest {
    InspectionRecord inspection;
    std::vector<DefectRecord> defects;
    std::vector<InspectionImage> images;
};

/// Writes an inspection, its defects and its image references as one atomic
/// unit. It sits between the application layer (which turns a Phase 1
/// InspectionResult into a request) and the repositories, and knows nothing
/// about OpenCV or the detection engine.
class InspectionPersistenceService {
public:
    explicit InspectionPersistenceService(DatabaseConnection& connection) : db_(connection) {}

    /// Persists the request inside a single transaction and returns the new
    /// inspection id. On any failure the transaction is rolled back, so no
    /// partial inspection, defect or image rows remain, and the exception is
    /// propagated to the caller.
    std::int64_t persist(const InspectionPersistenceRequest& request);

    /// Deletes an inspection with its defects and image references in one
    /// transaction. Physical files are not touched. Returns false if the
    /// inspection does not exist.
    bool deleteInspection(std::int64_t inspection_id);

private:
    DatabaseConnection& db_;
};

}  // namespace optinspect::database
