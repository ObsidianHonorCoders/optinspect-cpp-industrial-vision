#pragma once

#include "common/Errors.hpp"

#include <string>

namespace optinspect::database {

/// Thrown for any SQLite or database-layer failure. `code()` is the SQLite
/// primary result code (0 when the error did not originate from SQLite).
class DatabaseError : public OptInspectError {
public:
    explicit DatabaseError(const std::string& message, int code = 0, int extended_code = 0)
        : OptInspectError(message), code_(code), extended_code_(extended_code) {}

    int code() const { return code_; }
    int extendedCode() const { return extended_code_; }

private:
    int code_;
    int extended_code_;
};

enum class ConstraintKind { Unique, ForeignKey, Check, NotNull, Other };

/// Thrown when a statement violates a schema constraint (duplicate key,
/// missing referenced row, CHECK failure, ...). Distinguished from the base
/// class so callers can report "duplicate" or "still referenced" precisely.
class ConstraintViolationError : public DatabaseError {
public:
    ConstraintViolationError(const std::string& message, ConstraintKind kind, int code,
                              int extended_code)
        : DatabaseError(message, code, extended_code), kind_(kind) {}

    ConstraintKind kind() const { return kind_; }

private:
    ConstraintKind kind_;
};

}  // namespace optinspect::database
