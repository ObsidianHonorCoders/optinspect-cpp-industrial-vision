#pragma once

#include "database/DatabaseConnection.hpp"
#include "database/Models.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace optinspect::database {

/// CRUD access to the `defects` table (measured suspicious regions).
class DefectRepository {
public:
    explicit DefectRepository(DatabaseConnection& connection) : db_(connection) {}

    /// Inserts one defect and returns its id. Throws ConstraintViolationError
    /// if the inspection does not exist or a value violates a CHECK constraint.
    std::int64_t create(const DefectRecord& defect);

    /// Inserts all defects for one inspection atomically (all or none) using a
    /// single prepared statement. `inspection_id` of each element is overridden
    /// with the argument. Returns the number of rows inserted.
    int createMany(std::int64_t inspection_id, const std::vector<DefectRecord>& defects);

    std::optional<DefectRecord> getById(std::int64_t id);
    std::vector<DefectRecord> listByInspection(std::int64_t inspection_id);
    std::vector<DefectRecord> list(const DefectFilter& filter);

    /// Sets or clears the reviewer note. This is the only mutable field of a
    /// defect; measurements are immutable. Returns false if the id does not exist.
    bool updateReviewNote(std::int64_t id, const std::optional<std::string>& note);

    /// Deletes one defect row; the owning inspection's detected_defect_count
    /// is adjusted by a trigger. Returns false if the id does not exist.
    bool remove(std::int64_t id);

private:
    DatabaseConnection& db_;
};

}  // namespace optinspect::database
