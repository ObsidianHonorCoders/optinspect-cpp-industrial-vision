#include "database/DefectRepository.hpp"

#include "database/DatabaseTransaction.hpp"
#include "database/Statement.hpp"
#include "database/TimeUtil.hpp"

#include <vector>

namespace optinspect::database {

namespace {

const char* const kColumns =
    "defect_id, inspection_id, x, y, width, height, area, perimeter, centroid_x, centroid_y, "
    "aspect_ratio, confidence, review_note, created_at";

const char* const kInsert =
    "INSERT INTO defects (inspection_id, x, y, width, height, area, perimeter, centroid_x, "
    "centroid_y, aspect_ratio, confidence, review_note, created_at) VALUES (:inspection_id, :x, :y, "
    ":width, :height, :area, :perimeter, :centroid_x, :centroid_y, :aspect_ratio, :confidence, "
    ":review_note, :created_at)";

DefectRecord readDefect(const Statement& stmt) {
    DefectRecord d;
    d.id = stmt.columnInt64(0);
    d.inspection_id = stmt.columnInt64(1);
    d.x = stmt.columnInt(2);
    d.y = stmt.columnInt(3);
    d.width = stmt.columnInt(4);
    d.height = stmt.columnInt(5);
    d.area = stmt.columnDouble(6);
    d.perimeter = stmt.columnOptionalDouble(7);
    d.centroid_x = stmt.columnOptionalDouble(8);
    d.centroid_y = stmt.columnOptionalDouble(9);
    d.aspect_ratio = stmt.columnOptionalDouble(10);
    d.confidence = stmt.columnOptionalDouble(11);
    d.review_note = stmt.columnOptionalText(12);
    d.created_at = stmt.columnText(13);
    return d;
}

void bindDefect(Statement& stmt, std::int64_t inspection_id, const DefectRecord& d,
                const std::string& created_at) {
    stmt.bind(":inspection_id", inspection_id)
        .bind(":x", d.x)
        .bind(":y", d.y)
        .bind(":width", d.width)
        .bind(":height", d.height)
        .bind(":area", d.area)
        .bind(":perimeter", d.perimeter)
        .bind(":centroid_x", d.centroid_x)
        .bind(":centroid_y", d.centroid_y)
        .bind(":aspect_ratio", d.aspect_ratio)
        .bind(":confidence", d.confidence)
        .bind(":review_note", d.review_note)
        .bind(":created_at", created_at);
}

}  // namespace

std::int64_t DefectRepository::create(const DefectRecord& defect) {
    Statement stmt(db_, kInsert);
    bindDefect(stmt, defect.inspection_id, defect, nowUtcTimestamp());
    stmt.execute();
    return db_.lastInsertRowId();
}

int DefectRepository::createMany(std::int64_t inspection_id, const std::vector<DefectRecord>& defects) {
    if (defects.empty()) return 0;

    Transaction transaction(db_);
    Statement stmt(db_, kInsert);
    const std::string created_at = nowUtcTimestamp();
    for (const DefectRecord& defect : defects) {
        stmt.reset();
        bindDefect(stmt, inspection_id, defect, created_at);
        stmt.execute();
    }
    transaction.commit();
    return static_cast<int>(defects.size());
}

std::optional<DefectRecord> DefectRepository::getById(std::int64_t id) {
    Statement stmt(db_, std::string("SELECT ") + kColumns + " FROM defects WHERE defect_id = :id");
    stmt.bind(":id", id);
    if (!stmt.step()) return std::nullopt;
    return readDefect(stmt);
}

std::vector<DefectRecord> DefectRepository::listByInspection(std::int64_t inspection_id) {
    DefectFilter filter;
    filter.inspection_id = inspection_id;
    return list(filter);
}

std::vector<DefectRecord> DefectRepository::list(const DefectFilter& filter) {
    std::vector<std::string> conditions;
    if (filter.inspection_id) conditions.push_back("inspection_id = :inspection_id");
    if (filter.min_area) conditions.push_back("area >= :min_area");
    if (filter.max_area) conditions.push_back("area <= :max_area");
    if (filter.min_confidence) conditions.push_back("confidence >= :min_confidence");
    if (filter.max_confidence) conditions.push_back("confidence <= :max_confidence");

    std::string sql = std::string("SELECT ") + kColumns + " FROM defects";
    for (std::size_t n = 0; n < conditions.size(); ++n) {
        sql += (n == 0 ? " WHERE " : " AND ") + conditions[n];
    }
    sql += " ORDER BY inspection_id, defect_id LIMIT :limit";

    Statement stmt(db_, sql);
    if (filter.inspection_id) stmt.bind(":inspection_id", *filter.inspection_id);
    if (filter.min_area) stmt.bind(":min_area", *filter.min_area);
    if (filter.max_area) stmt.bind(":max_area", *filter.max_area);
    if (filter.min_confidence) stmt.bind(":min_confidence", *filter.min_confidence);
    if (filter.max_confidence) stmt.bind(":max_confidence", *filter.max_confidence);
    stmt.bind(":limit", filter.limit.value_or(-1));

    std::vector<DefectRecord> defects;
    while (stmt.step()) defects.push_back(readDefect(stmt));
    return defects;
}

bool DefectRepository::updateReviewNote(std::int64_t id, const std::optional<std::string>& note) {
    Statement stmt(db_, "UPDATE defects SET review_note = :note WHERE defect_id = :id");
    stmt.bind(":note", note).bind(":id", id);
    stmt.execute();
    return db_.changes() > 0;
}

bool DefectRepository::remove(std::int64_t id) {
    Statement stmt(db_, "DELETE FROM defects WHERE defect_id = :id");
    stmt.bind(":id", id);
    stmt.execute();
    return db_.changes() > 0;
}

}  // namespace optinspect::database
