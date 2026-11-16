#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace optinspect {

/// True if `word` names one of the database command groups
/// (db, product, inspection, defect, analytics).
bool isDatabaseCommand(const std::string& word);

/// Help text for the database command groups.
std::string databaseUsageText();

/// Runs a database command group. `args[0]` is the group name (for example
/// {"product", "list", "--database", "x.db"}). Returns the process exit code:
/// 0 success, 64 usage error, 66 record not found, 70 runtime error,
/// 74 database error. Output goes to `out`, diagnostics to `err`.
int runDatabaseCommand(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace optinspect
