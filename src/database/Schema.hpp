#pragma once

#include <string>
#include <vector>

namespace optinspect::database {

/// One forward-only schema migration. `version` is the value of
/// `PRAGMA user_version` after the migration has been applied.
struct Migration {
    int version;
    const char* description;
    std::string sql;
};

/// All known migrations in ascending version order. To change the schema,
/// append a new Migration with the next version number; never edit a
/// migration that has already shipped.
const std::vector<Migration>& migrations();

int latestSchemaVersion();

}  // namespace optinspect::database
