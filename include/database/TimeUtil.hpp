#pragma once

#include <string>

namespace optinspect::database {

/// Current UTC time as "YYYY-MM-DDTHH:MM:SSZ". All timestamps stored by the
/// database layer use this format: it sorts lexicographically in
/// chronological order and is understood by SQLite's date functions.
std::string nowUtcTimestamp();

/// Converts a local-time broken-down time ("YYYYMMDD-HHMMSS", the Phase 1
/// inspection-id prefix) to a UTC timestamp. Returns an empty string if the
/// text does not start with a valid stamp.
std::string utcTimestampFromLocalStamp(const std::string& stamp);

/// True for "YYYY-MM-DD" or "YYYY-MM-DDTHH:MM:SSZ" with plausible field ranges.
bool isValidDateOrTimestamp(const std::string& text);

}  // namespace optinspect::database
