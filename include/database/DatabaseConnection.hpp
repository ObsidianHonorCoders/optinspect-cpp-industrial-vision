#pragma once

#include <cstdint>
#include <string>

struct sqlite3;

namespace optinspect::database {

enum class OpenMode {
    ReadWriteCreate,  ///< Open read/write, creating the file if it does not exist.
    ReadWrite,        ///< Open read/write; fail if the file does not exist.
    ReadOnly,         ///< Open read-only; fail if the file does not exist.
};

/// RAII owner of a SQLite connection. Move-only. Foreign-key enforcement is
/// switched on for every connection (SQLite leaves it off by default) and a
/// busy timeout is installed so a concurrent writer causes a short wait
/// instead of an immediate SQLITE_BUSY error.
///
/// The class performs no schema management; see DatabaseInitializer.
class DatabaseConnection {
public:
    /// Opens a database file. Throws DatabaseError if it cannot be opened
    /// (for example, the parent directory does not exist). The parent
    /// directory is never created implicitly.
    static DatabaseConnection open(const std::string& path,
                                   OpenMode mode = OpenMode::ReadWriteCreate);

    /// Opens a private in-memory database (used by tests and benchmarks).
    static DatabaseConnection openInMemory();

    DatabaseConnection(const DatabaseConnection&) = delete;
    DatabaseConnection& operator=(const DatabaseConnection&) = delete;
    DatabaseConnection(DatabaseConnection&& other) noexcept;
    DatabaseConnection& operator=(DatabaseConnection&& other) noexcept;
    ~DatabaseConnection();

    /// Executes one or more SQL statements that take no parameters
    /// (pragmas, DDL, transaction control). Do not use for user input.
    void execute(const std::string& sql);

    /// Runs `PRAGMA <name>` and returns the first column of the first row as
    /// text (empty if the pragma returns nothing).
    std::string pragmaValue(const std::string& name);

    std::int64_t lastInsertRowId() const;
    int changes() const;

    /// True while an explicit transaction is open.
    bool inTransaction() const;

    const std::string& path() const { return path_; }
    sqlite3* handle() const { return db_; }

    /// Throws a DatabaseError / ConstraintViolationError describing the most
    /// recent failure of this connection, prefixed with `context`.
    [[noreturn]] void throwLastError(const std::string& context) const;

private:
    DatabaseConnection(sqlite3* db, std::string path);
    void close() noexcept;

    sqlite3* db_ = nullptr;
    std::string path_;
};

/// Version string of the linked SQLite library, e.g. "3.54.1".
std::string sqliteLibraryVersion();

}  // namespace optinspect::database
