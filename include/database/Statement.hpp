#pragma once

#include "database/DatabaseConnection.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

struct sqlite3_stmt;

namespace optinspect::database {

/// RAII wrapper for a prepared statement. All values are passed through
/// bind(); SQL text must never be assembled from user-supplied values.
///
/// Parameters are bound by name (":name"), which keeps queries readable and
/// independent of parameter order. Column indices are 0-based.
class Statement {
public:
    Statement(DatabaseConnection& connection, const std::string& sql);
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    ~Statement();

    Statement& bind(const char* name, std::nullptr_t);
    Statement& bind(const char* name, double value);
    Statement& bind(const char* name, const std::string& value);
    Statement& bind(const char* name, const char* value);

    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    Statement& bind(const char* name, T value) {
        return bindInt64(name, static_cast<std::int64_t>(value));
    }

    template <typename T>
    Statement& bind(const char* name, const std::optional<T>& value) {
        if (value) return bind(name, *value);
        return bind(name, nullptr);
    }

    /// Advances to the next row. Returns true while a row is available and
    /// false once the statement is done. Throws on error or constraint failure.
    bool step();

    /// Runs a statement that returns no rows (INSERT/UPDATE/DELETE/DDL).
    void execute();

    /// Clears the current row state and all bindings so the statement can be reused.
    void reset();

    bool isNull(int column) const;
    std::int64_t columnInt64(int column) const;
    int columnInt(int column) const;
    double columnDouble(int column) const;
    std::string columnText(int column) const;

    std::optional<std::int64_t> columnOptionalInt64(int column) const;
    std::optional<double> columnOptionalDouble(int column) const;
    std::optional<std::string> columnOptionalText(int column) const;

private:
    Statement& bindInt64(const char* name, std::int64_t value);
    int parameterIndex(const char* name) const;

    DatabaseConnection* connection_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

}  // namespace optinspect::database
