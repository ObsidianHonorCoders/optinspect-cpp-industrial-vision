#pragma once

#include "database/Models.hpp"
#include "database/Statement.hpp"

#include <string>

namespace optinspect::database::detail {

/// Builds " WHERE ..." (or "") for an InspectionFilter against a table
/// aliased as `i`. Only fixed SQL fragments are concatenated; every value is
/// bound by bindInspectionFilter(). Throws std::invalid_argument for
/// malformed date arguments.
std::string inspectionWhereClause(const InspectionFilter& filter);

void bindInspectionFilter(Statement& stmt, const InspectionFilter& filter);

}  // namespace optinspect::database::detail
