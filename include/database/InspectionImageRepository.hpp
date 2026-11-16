#pragma once

#include "database/DatabaseConnection.hpp"
#include "database/Models.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace optinspect::database {

/// CRUD access to `inspection_images`: references (paths + metadata) to image
/// files. The files themselves are never read, written or deleted here.
class InspectionImageRepository {
public:
    explicit InspectionImageRepository(DatabaseConnection& connection) : db_(connection) {}

    /// Inserts an image reference and returns its id. At most one image of
    /// each type may exist per inspection (Unique violation otherwise).
    std::int64_t create(const InspectionImage& image);

    std::optional<InspectionImage> getById(std::int64_t id);
    std::optional<InspectionImage> getByInspectionAndType(std::int64_t inspection_id, ImageType type);
    std::vector<InspectionImage> listByInspection(std::int64_t inspection_id);

    /// Updates file_path, width and height. Returns false if the id does not exist.
    bool update(const InspectionImage& image);

    /// Removes the database reference only; the file on disk is left alone.
    bool remove(std::int64_t id);

private:
    DatabaseConnection& db_;
};

}  // namespace optinspect::database
