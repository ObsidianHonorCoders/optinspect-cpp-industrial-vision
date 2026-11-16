#pragma once

#include "database/DatabaseConnection.hpp"

#include <vector>

namespace optinspect::database {

struct InitializationResult {
    int previous_version = 0;
    int current_version = 0;
    std::vector<int> applied_versions;  ///< Migrations applied by this call (empty if up to date).
    bool created = false;               ///< True when the schema was created from scratch.
};

/// Creates or upgrades the OptiInspect schema.
///
/// The schema version is stored in `PRAGMA user_version`. Initialization is
/// idempotent: calling it on an up-to-date database changes nothing. It never
/// drops or recreates existing user data, and it refuses to touch a database
/// that already contains tables but no OptiInspect schema version.
class DatabaseInitializer {
public:
    /// Brings the database up to the latest schema version. Each migration is
    /// applied in its own transaction together with its version bump, so a
    /// failed migration leaves the previous version intact.
    /// Throws DatabaseError if the database is newer than this build, is not
    /// an OptiInspect database, or a migration fails.
    static InitializationResult initialize(DatabaseConnection& connection);

    /// Schema version recorded in the database (0 = never initialized).
    static int schemaVersion(DatabaseConnection& connection);

    /// Throws DatabaseError unless the database is at exactly the latest
    /// schema version. Used by commands that must not modify the schema.
    static void requireCurrentSchema(DatabaseConnection& connection);

    static int latestVersion();
};

}  // namespace optinspect::database
