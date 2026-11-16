#include "database/InspectionRepository.hpp"

#include "InspectionQuery.hpp"
#include "database/DatabaseError.hpp"
#include "database/DatabaseTransaction.hpp"
#include "database/Statement.hpp"
#include "database/TimeUtil.hpp"

#include <stdexcept>

namespace optinspect::database {

namespace {

const char* const kSelect =
    "SELECT i.inspection_id, i.external_id, i.product_id, i.serial_number, "
    "i.inspection_timestamp, i.status, i.decision_reason, i.alignment_status, "
    "i.alignment_confidence, i.report_path, i.detected_defect_count, i.processing_time_ms, "
    "c.config_json, i.configuration_id, i.notes, i.created_at "
    "FROM inspections i LEFT JOIN configurations c ON c.configuration_id = i.configuration_id";

InspectionRecord readInspection(const Statement& stmt) {
    InspectionRecord r;
    r.id = stmt.columnInt64(0);
    r.external_id = stmt.columnOptionalText(1);
    r.product_id = stmt.columnOptionalInt64(2);
    r.serial_number = stmt.columnOptionalText(3);
    r.inspection_timestamp = stmt.columnText(4);
    r.status = parseInspectionStatus(stmt.columnText(5));
    r.decision_reason = stmt.columnOptionalText(6);
    if (const auto alignment = stmt.columnOptionalText(7)) {
        r.alignment_status = parseAlignmentStatus(*alignment);
    }
    r.alignment_confidence = stmt.columnOptionalDouble(8);
    r.report_path = stmt.columnOptionalText(9);
    r.detected_defect_count = stmt.columnInt(10);
    r.processing_time_ms = stmt.columnOptionalDouble(11);
    r.configuration_snapshot = stmt.columnOptionalText(12);
    r.configuration_id = stmt.columnOptionalInt64(13);
    r.notes = stmt.columnOptionalText(14);
    r.created_at = stmt.columnText(15);
    return r;
}

}  // namespace

std::int64_t InspectionRepository::upsertConfiguration(const std::string& config_json) {
    {
        Statement find(db_, "SELECT configuration_id FROM configurations WHERE config_json = :json");
        find.bind(":json", config_json);
        if (find.step()) return find.columnInt64(0);
    }
    Statement insert(db_,
                     "INSERT INTO configurations (config_json, created_at) VALUES (:json, :created_at)");
    insert.bind(":json", config_json).bind(":created_at", nowUtcTimestamp());
    insert.execute();
    return db_.lastInsertRowId();
}

std::int64_t InspectionRepository::create(const InspectionRecord& inspection) {
    if (inspection.inspection_timestamp.size() != 20 ||
        !isValidDateOrTimestamp(inspection.inspection_timestamp)) {
        throw std::invalid_argument("Invalid inspection_timestamp '" + inspection.inspection_timestamp +
                                    "' (expected YYYY-MM-DDTHH:MM:SSZ).");
    }

    Transaction transaction(db_);

    std::optional<std::int64_t> configuration_id;
    if (inspection.configuration_snapshot) {
        configuration_id = upsertConfiguration(*inspection.configuration_snapshot);
    }

    Statement stmt(
        db_,
        "INSERT INTO inspections (external_id, product_id, serial_number, inspection_timestamp, "
        "status, decision_reason, alignment_status, alignment_confidence, report_path, "
        "processing_time_ms, configuration_id, notes, created_at) "
        "VALUES (:external_id, :product_id, :serial_number, :timestamp, :status, :reason, "
        ":alignment_status, :alignment_confidence, :report_path, :processing_time_ms, "
        ":configuration_id, :notes, :created_at)");

    std::optional<std::string> alignment;
    if (inspection.alignment_status) alignment = toString(*inspection.alignment_status);

    stmt.bind(":external_id", inspection.external_id)
        .bind(":product_id", inspection.product_id)
        .bind(":serial_number", inspection.serial_number)
        .bind(":timestamp", inspection.inspection_timestamp)
        .bind(":status", toString(inspection.status))
        .bind(":reason", inspection.decision_reason)
        .bind(":alignment_status", alignment)
        .bind(":alignment_confidence", inspection.alignment_confidence)
        .bind(":report_path", inspection.report_path)
        .bind(":processing_time_ms", inspection.processing_time_ms)
        .bind(":configuration_id", configuration_id)
        .bind(":notes", inspection.notes)
        .bind(":created_at", nowUtcTimestamp());
    stmt.execute();

    const std::int64_t id = db_.lastInsertRowId();
    transaction.commit();
    return id;
}

std::optional<InspectionRecord> InspectionRepository::getById(std::int64_t id) {
    Statement stmt(db_, std::string(kSelect) + " WHERE i.inspection_id = :id");
    stmt.bind(":id", id);
    if (!stmt.step()) return std::nullopt;
    return readInspection(stmt);
}

std::optional<InspectionRecord> InspectionRepository::getByExternalId(const std::string& external_id) {
    Statement stmt(db_, std::string(kSelect) + " WHERE i.external_id = :external_id");
    stmt.bind(":external_id", external_id);
    if (!stmt.step()) return std::nullopt;
    return readInspection(stmt);
}

std::vector<InspectionRecord> InspectionRepository::list(const InspectionFilter& filter) {
    Statement stmt(db_, std::string(kSelect) + detail::inspectionWhereClause(filter) +
                            " ORDER BY i.inspection_timestamp DESC, i.inspection_id DESC "
                            "LIMIT :limit OFFSET :offset");
    detail::bindInspectionFilter(stmt, filter);
    stmt.bind(":limit", filter.limit.value_or(-1)).bind(":offset", filter.offset);

    std::vector<InspectionRecord> records;
    while (stmt.step()) records.push_back(readInspection(stmt));
    return records;
}

int InspectionRepository::count(const InspectionFilter& filter) {
    Statement stmt(db_,
                   "SELECT COUNT(*) FROM inspections i" + detail::inspectionWhereClause(filter));
    detail::bindInspectionFilter(stmt, filter);
    stmt.step();
    return stmt.columnInt(0);
}

bool InspectionRepository::updateMetadata(const InspectionRecord& inspection) {
    Statement stmt(db_,
                   "UPDATE inspections SET product_id = :product_id, serial_number = :serial_number, "
                   "report_path = :report_path, notes = :notes WHERE inspection_id = :id");
    stmt.bind(":product_id", inspection.product_id)
        .bind(":serial_number", inspection.serial_number)
        .bind(":report_path", inspection.report_path)
        .bind(":notes", inspection.notes)
        .bind(":id", inspection.id);
    stmt.execute();
    return db_.changes() > 0;
}

bool InspectionRepository::remove(std::int64_t id) {
    Transaction transaction(db_);
    Statement stmt(db_, "DELETE FROM inspections WHERE inspection_id = :id");
    stmt.bind(":id", id);
    stmt.execute();
    const bool deleted = db_.changes() > 0;
    transaction.commit();
    return deleted;
}

}  // namespace optinspect::database
