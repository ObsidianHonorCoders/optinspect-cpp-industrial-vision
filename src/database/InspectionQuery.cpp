#include "InspectionQuery.hpp"

#include "database/TimeUtil.hpp"

#include <stdexcept>
#include <vector>

namespace optinspect::database::detail {

namespace {

void requireDate(const std::optional<std::string>& value, const char* name) {
    if (value && !isValidDateOrTimestamp(*value)) {
        throw std::invalid_argument(std::string("Invalid ") + name + " '" + *value +
                                    "' (expected YYYY-MM-DD or YYYY-MM-DDTHH:MM:SSZ).");
    }
}

}  // namespace

std::string inspectionWhereClause(const InspectionFilter& filter) {
    requireDate(filter.from, "from date");
    requireDate(filter.to, "to date");

    std::vector<std::string> conditions;
    if (filter.product_id) conditions.push_back("i.product_id = :product_id");
    if (filter.serial_number) conditions.push_back("i.serial_number = :serial_number");
    if (filter.status) conditions.push_back("i.status = :status");
    if (filter.from) conditions.push_back("i.inspection_timestamp >= :from");
    if (filter.to) {
        // A bare date means "through the end of that day".
        conditions.push_back(filter.to->size() == 10
                                 ? "i.inspection_timestamp < date(:to, '+1 day')"
                                 : "i.inspection_timestamp < :to");
    }

    std::string where;
    for (std::size_t n = 0; n < conditions.size(); ++n) {
        where += (n == 0 ? " WHERE " : " AND ") + conditions[n];
    }
    return where;
}

void bindInspectionFilter(Statement& stmt, const InspectionFilter& filter) {
    if (filter.product_id) stmt.bind(":product_id", *filter.product_id);
    if (filter.serial_number) stmt.bind(":serial_number", *filter.serial_number);
    if (filter.status) stmt.bind(":status", toString(*filter.status));
    if (filter.from) stmt.bind(":from", *filter.from);
    if (filter.to) stmt.bind(":to", *filter.to);
}

}  // namespace optinspect::database::detail
