#include "database/DatabaseConnection.hpp"

#include "database/DatabaseError.hpp"
#include "database/Statement.hpp"

#include <sqlite3.h>

#include <utility>

namespace optinspect::database {

namespace {

constexpr int kBusyTimeoutMs = 5000;

// ON DELETE RESTRICT is implemented by SQLite as a trigger, so it reports
// SQLITE_CONSTRAINT_TRIGGER; the message is the only way to recognise it.
ConstraintKind constraintKindFor(int extended_code, const std::string& message) {
    switch (extended_code) {
        case SQLITE_CONSTRAINT_UNIQUE:
        case SQLITE_CONSTRAINT_PRIMARYKEY: return ConstraintKind::Unique;
        case SQLITE_CONSTRAINT_FOREIGNKEY: return ConstraintKind::ForeignKey;
        case SQLITE_CONSTRAINT_CHECK: return ConstraintKind::Check;
        case SQLITE_CONSTRAINT_NOTNULL: return ConstraintKind::NotNull;
        case SQLITE_CONSTRAINT_TRIGGER:
            return message.find("FOREIGN KEY") != std::string::npos ? ConstraintKind::ForeignKey
                                                                     : ConstraintKind::Other;
        default: return ConstraintKind::Other;
    }
}

}  // namespace

std::string sqliteLibraryVersion() { return sqlite3_libversion(); }

DatabaseConnection::DatabaseConnection(sqlite3* db, std::string path)
    : db_(db), path_(std::move(path)) {}

DatabaseConnection DatabaseConnection::open(const std::string& path, OpenMode mode) {
    int flags = 0;
    switch (mode) {
        case OpenMode::ReadWriteCreate: flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE; break;
        case OpenMode::ReadWrite: flags = SQLITE_OPEN_READWRITE; break;
        case OpenMode::ReadOnly: flags = SQLITE_OPEN_READONLY; break;
    }
    if (path.empty()) {
        throw DatabaseError("Cannot open database: path is empty.");
    }
    flags |= SQLITE_OPEN_FULLMUTEX;

    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &raw, flags, nullptr);
    if (rc != SQLITE_OK) {
        const std::string detail = raw ? sqlite3_errmsg(raw) : sqlite3_errstr(rc);
        const int extended = raw ? sqlite3_extended_errcode(raw) : rc;
        if (raw) sqlite3_close(raw);
        throw DatabaseError("Cannot open database '" + path + "': " + detail, rc, extended);
    }

    DatabaseConnection connection(raw, path);
    sqlite3_busy_timeout(raw, kBusyTimeoutMs);
    connection.execute("PRAGMA foreign_keys = ON");
    if (connection.pragmaValue("foreign_keys") != "1") {
        throw DatabaseError("SQLite was built without foreign key support; refusing to continue.");
    }
    return connection;
}

DatabaseConnection DatabaseConnection::openInMemory() { return open(":memory:"); }

DatabaseConnection::DatabaseConnection(DatabaseConnection&& other) noexcept
    : db_(other.db_), path_(std::move(other.path_)) {
    other.db_ = nullptr;
}

DatabaseConnection& DatabaseConnection::operator=(DatabaseConnection&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        path_ = std::move(other.path_);
        other.db_ = nullptr;
    }
    return *this;
}

DatabaseConnection::~DatabaseConnection() { close(); }

void DatabaseConnection::close() noexcept {
    if (db_ != nullptr) {
        // sqlite3_close_v2 defers destruction if statements are still alive,
        // which avoids failures when destruction order is not ideal.
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

void DatabaseConnection::execute(const std::string& sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errstr(rc);
        sqlite3_free(error);
        const int extended = sqlite3_extended_errcode(db_);
        if ((rc & 0xFF) == SQLITE_CONSTRAINT) {
            throw ConstraintViolationError("SQL execution failed: " + message,
                                           constraintKindFor(extended, message), rc, extended);
        }
        throw DatabaseError("SQL execution failed: " + message, rc, extended);
    }
}

std::string DatabaseConnection::pragmaValue(const std::string& name) {
    Statement stmt(*this, "PRAGMA " + name);
    if (stmt.step() && !stmt.isNull(0)) {
        return stmt.columnText(0);
    }
    return {};
}

std::int64_t DatabaseConnection::lastInsertRowId() const {
    return sqlite3_last_insert_rowid(db_);
}

int DatabaseConnection::changes() const { return sqlite3_changes(db_); }

bool DatabaseConnection::inTransaction() const { return sqlite3_get_autocommit(db_) == 0; }

void DatabaseConnection::throwLastError(const std::string& context) const {
    const int extended = sqlite3_extended_errcode(db_);
    const int primary = extended & 0xFF;
    const std::string message = context + ": " + sqlite3_errmsg(db_);
    if (primary == SQLITE_CONSTRAINT) {
        throw ConstraintViolationError(message, constraintKindFor(extended, message), primary, extended);
    }
    throw DatabaseError(message, primary, extended);
}

}  // namespace optinspect::database
