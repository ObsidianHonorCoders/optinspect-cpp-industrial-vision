#include "database/InspectionImageRepository.hpp"

#include "database/Statement.hpp"
#include "database/TimeUtil.hpp"

namespace optinspect::database {

namespace {

const char* const kColumns =
    "image_id, inspection_id, image_type, file_path, width, height, created_at";

InspectionImage readImage(const Statement& stmt) {
    InspectionImage img;
    img.id = stmt.columnInt64(0);
    img.inspection_id = stmt.columnInt64(1);
    img.type = parseImageType(stmt.columnText(2));
    img.file_path = stmt.columnText(3);
    if (const auto w = stmt.columnOptionalInt64(4)) img.width = static_cast<int>(*w);
    if (const auto h = stmt.columnOptionalInt64(5)) img.height = static_cast<int>(*h);
    img.created_at = stmt.columnText(6);
    return img;
}

}  // namespace

std::int64_t InspectionImageRepository::create(const InspectionImage& image) {
    Statement stmt(db_,
                   "INSERT INTO inspection_images (inspection_id, image_type, file_path, width, "
                   "height, created_at) VALUES (:inspection_id, :type, :path, :width, :height, "
                   ":created_at)");
    stmt.bind(":inspection_id", image.inspection_id)
        .bind(":type", toString(image.type))
        .bind(":path", image.file_path)
        .bind(":width", image.width)
        .bind(":height", image.height)
        .bind(":created_at", nowUtcTimestamp());
    stmt.execute();
    return db_.lastInsertRowId();
}

std::optional<InspectionImage> InspectionImageRepository::getById(std::int64_t id) {
    Statement stmt(db_,
                   std::string("SELECT ") + kColumns + " FROM inspection_images WHERE image_id = :id");
    stmt.bind(":id", id);
    if (!stmt.step()) return std::nullopt;
    return readImage(stmt);
}

std::optional<InspectionImage> InspectionImageRepository::getByInspectionAndType(
    std::int64_t inspection_id, ImageType type) {
    Statement stmt(db_, std::string("SELECT ") + kColumns +
                            " FROM inspection_images WHERE inspection_id = :inspection_id AND "
                            "image_type = :type");
    stmt.bind(":inspection_id", inspection_id).bind(":type", toString(type));
    if (!stmt.step()) return std::nullopt;
    return readImage(stmt);
}

std::vector<InspectionImage> InspectionImageRepository::listByInspection(std::int64_t inspection_id) {
    Statement stmt(db_, std::string("SELECT ") + kColumns +
                            " FROM inspection_images WHERE inspection_id = :inspection_id "
                            "ORDER BY image_id");
    stmt.bind(":inspection_id", inspection_id);
    std::vector<InspectionImage> images;
    while (stmt.step()) images.push_back(readImage(stmt));
    return images;
}

bool InspectionImageRepository::update(const InspectionImage& image) {
    Statement stmt(db_,
                   "UPDATE inspection_images SET file_path = :path, width = :width, height = :height "
                   "WHERE image_id = :id");
    stmt.bind(":path", image.file_path)
        .bind(":width", image.width)
        .bind(":height", image.height)
        .bind(":id", image.id);
    stmt.execute();
    return db_.changes() > 0;
}

bool InspectionImageRepository::remove(std::int64_t id) {
    Statement stmt(db_, "DELETE FROM inspection_images WHERE image_id = :id");
    stmt.bind(":id", id);
    stmt.execute();
    return db_.changes() > 0;
}

}  // namespace optinspect::database
