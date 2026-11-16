#pragma once

#include "database/DatabaseConnection.hpp"
#include "database/Models.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace optinspect::database {

/// CRUD access to `inspections` (plus the shared `configurations` table).
class InspectionRepository {
public:
    explicit InspectionRepository(DatabaseConnection& connection) : db_(connection) {}

    /// Inserts an inspection and returns its id. `id`, `created_at`,
    /// `detected_defect_count` and `configuration_id` of the argument are
    /// ignored. If `configuration_snapshot` is set it is stored once in
    /// `configurations` and shared by every inspection that used the same text.
    /// Throws std::invalid_argument for a malformed timestamp and
    /// ConstraintViolationError for constraint failures (unknown product,
    /// invalid status, duplicate external id, ...).
    std::int64_t create(const InspectionRecord& inspection);

    std::optional<InspectionRecord> getById(std::int64_t id);
    std::optional<InspectionRecord> getByExternalId(const std::string& external_id);

    /// History query, newest first. All filter fields are optional and combined with AND.
    std::vector<InspectionRecord> list(const InspectionFilter& filter = {});
    int count(const InspectionFilter& filter = {});

    /// Updates the mutable metadata only: product_id, serial_number,
    /// report_path and notes. The decision, timestamp, measurements and
    /// configuration of a recorded inspection are immutable (enforced by a
    /// database trigger as well). Returns false if the id does not exist.
    bool updateMetadata(const InspectionRecord& inspection);

    /// Deletes an inspection together with its defect and image-reference rows.
    /// Image files on disk are never touched. Returns false if the id does not exist.
    bool remove(std::int64_t id);

private:
    std::int64_t upsertConfiguration(const std::string& config_json);

    DatabaseConnection& db_;
};

}  // namespace optinspect::database
