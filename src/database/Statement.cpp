#include "database/Statement.hpp"

#include "database/DatabaseError.hpp"

#include <sqlite3.h>

#include <utility>

namespace optinspect::database {

Statement::Statement(DatabaseConnection& connection, const std::string& sql)
    : connection_(&connection) {
    const int rc = sqlite3_prepare_v2(connection.handle(), sql.c_str(),
                                      static_cast<int>(sql.size()), &stmt_, nullptr);
    if (rc != SQLITE_OK) {
        connection.throwLastError("Failed to prepare statement");
    }
    if (stmt_ == nullptr) {
        throw DatabaseError("Failed to prepare statement: SQL contains no statement.");
    }
}

Statement::Statement(Statement&& other) noexcept
    : connection_(other.connection_), stmt_(other.stmt_) {
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) sqlite3_finalize(stmt_);
        connection_ = other.connection_;
        stmt_ = other.stmt_;
        other.stmt_ = nullptr;
    }
    return *this;
}

Statement::~Statement() {
    if (stmt_ != nullptr) sqlite3_finalize(stmt_);
}

int Statement::parameterIndex(const char* name) const {
    const int index = sqlite3_bind_parameter_index(stmt_, name);
    if (index == 0) {
        throw DatabaseError(std::string("Unknown SQL parameter: ") + name);
    }
    return index;
}

Statement& Statement::bind(const char* name, std::nullptr_t) {
    if (sqlite3_bind_null(stmt_, parameterIndex(name)) != SQLITE_OK) {
        connection_->throwLastError("Failed to bind NULL");
    }
    return *this;
}

Statement& Statement::bind(const char* name, double value) {
    if (sqlite3_bind_double(stmt_, parameterIndex(name), value) != SQLITE_OK) {
        connection_->throwLastError("Failed to bind REAL");
    }
    return *this;
}

Statement& Statement::bind(const char* name, const std::string& value) {
    // SQLITE_TRANSIENT: SQLite copies the text, so the argument may be a temporary.
    if (sqlite3_bind_text(stmt_, parameterIndex(name), value.c_str(),
                          static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        connection_->throwLastError("Failed to bind TEXT");
    }
    return *this;
}

Statement& Statement::bind(const char* name, const char* value) {
    if (value == nullptr) return bind(name, nullptr);
    return bind(name, std::string(value));
}

Statement& Statement::bindInt64(const char* name, std::int64_t value) {
    if (sqlite3_bind_int64(stmt_, parameterIndex(name), value) != SQLITE_OK) {
        connection_->throwLastError("Failed to bind INTEGER");
    }
    return *this;
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    connection_->throwLastError("Statement execution failed");
}

void Statement::execute() {
    while (step()) {
    }
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

bool Statement::isNull(int column) const {
    return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

std::int64_t Statement::columnInt64(int column) const { return sqlite3_column_int64(stmt_, column); }

int Statement::columnInt(int column) const { return sqlite3_column_int(stmt_, column); }

double Statement::columnDouble(int column) const { return sqlite3_column_double(stmt_, column); }

std::string Statement::columnText(int column) const {
    const unsigned char* text = sqlite3_column_text(stmt_, column);
    if (text == nullptr) return {};
    return std::string(reinterpret_cast<const char*>(text),
                       static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column)));
}

std::optional<std::int64_t> Statement::columnOptionalInt64(int column) const {
    if (isNull(column)) return std::nullopt;
    return columnInt64(column);
}

std::optional<double> Statement::columnOptionalDouble(int column) const {
    if (isNull(column)) return std::nullopt;
    return columnDouble(column);
}

std::optional<std::string> Statement::columnOptionalText(int column) const {
    if (isNull(column)) return std::nullopt;
    return columnText(column);
}

}  // namespace optinspect::database
