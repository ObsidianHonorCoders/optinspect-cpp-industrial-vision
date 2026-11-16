#include "database/DatabaseInitializer.hpp"

#include "Schema.hpp"
#include "database/DatabaseError.hpp"
#include "database/DatabaseTransaction.hpp"
#include "database/Statement.hpp"

#include <sqlite3.h>

#include <string>

namespace optinspect::database {

namespace {

// Window functions (used by the analytics queries) need SQLite 3.25.0.
constexpr int kMinimumSqliteVersion = 3025000;

bool hasUserTables(DatabaseConnection& connection) {
    Statement stmt(connection,
                   "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' "
                   "LIMIT 1");
    return stmt.step();
}

}  // namespace

int DatabaseInitializer::latestVersion() { return latestSchemaVersion(); }

int DatabaseInitializer::schemaVersion(DatabaseConnection& connection) {
    Statement stmt(connection, "PRAGMA user_version");
    return stmt.step() ? stmt.columnInt(0) : 0;
}

InitializationResult DatabaseInitializer::initialize(DatabaseConnection& connection) {
    if (sqlite3_libversion_number() < kMinimumSqliteVersion) {
        throw DatabaseError("SQLite " + sqliteLibraryVersion() +
                            " is too old; OptiInspect requires 3.25.0 or newer.");
    }

    InitializationResult result;
    result.previous_version = schemaVersion(connection);
    result.current_version = result.previous_version;

    if (result.previous_version > latestVersion()) {
        throw DatabaseError("Database schema version " + std::to_string(result.previous_version) +
                            " is newer than the version supported by this build (" +
                            std::to_string(latestVersion()) + "). Upgrade OptiInspect.");
    }

    if (result.previous_version == 0 && hasUserTables(connection)) {
        throw DatabaseError("Refusing to initialize '" + connection.path() +
                            "': it already contains tables but no OptiInspect schema version. "
                            "Choose a different database path.");
    }

    // Write-ahead logging lets analytics readers run while an inspection is
    // being written. It does not apply to in-memory databases, where SQLite
    // silently keeps the "memory" journal mode.
    if (connection.path() != ":memory:") {
        connection.execute("PRAGMA journal_mode = WAL");
    }

    for (const Migration& migration : migrations()) {
        if (migration.version <= result.previous_version) continue;

        Transaction transaction(connection);
        try {
            connection.execute(migration.sql);
            connection.execute("PRAGMA user_version = " + std::to_string(migration.version));
        } catch (const DatabaseError& e) {
            throw DatabaseError("Migration to schema version " + std::to_string(migration.version) +
                                    " failed: " + e.what(),
                                e.code(), e.extendedCode());
        }
        transaction.commit();
        result.applied_versions.push_back(migration.version);
        result.current_version = migration.version;
    }

    result.created = result.previous_version == 0 && !result.applied_versions.empty();
    return result;
}

void DatabaseInitializer::requireCurrentSchema(DatabaseConnection& connection) {
    const int version = schemaVersion(connection);
    if (version == 0) {
        throw DatabaseError("Database '" + connection.path() +
                            "' is not initialized. Run: optinspect db init --database <path>");
    }
    if (version < latestVersion()) {
        throw DatabaseError("Database schema version " + std::to_string(version) +
                            " is older than the required version " +
                            std::to_string(latestVersion()) +
                            ". Run: optinspect db init --database <path> to upgrade.");
    }
    if (version > latestVersion()) {
        throw DatabaseError("Database schema version " + std::to_string(version) +
                            " is newer than the version supported by this build (" +
                            std::to_string(latestVersion()) + ").");
    }
}

}  // namespace optinspect::database
