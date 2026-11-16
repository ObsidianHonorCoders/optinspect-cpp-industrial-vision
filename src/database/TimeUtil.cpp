#include "database/TimeUtil.hpp"

#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace optinspect::database {

namespace {

std::tm toUtc(std::time_t t) {
    std::tm out{};
#if defined(_WIN32)
    gmtime_s(&out, &t);
#else
    gmtime_r(&t, &out);
#endif
    return out;
}

std::string format(const std::tm& tm) {
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

bool digitsAt(const std::string& s, std::size_t pos, std::size_t count) {
    if (pos + count > s.size()) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[pos + i]))) return false;
    }
    return true;
}

int numberAt(const std::string& s, std::size_t pos, std::size_t count) {
    return std::stoi(s.substr(pos, count));
}

}  // namespace

std::string nowUtcTimestamp() {
    return format(toUtc(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())));
}

std::string utcTimestampFromLocalStamp(const std::string& stamp) {
    if (!digitsAt(stamp, 0, 8) || stamp.size() < 15 || stamp[8] != '-' || !digitsAt(stamp, 9, 6)) {
        return {};
    }
    std::tm local{};
    local.tm_year = numberAt(stamp, 0, 4) - 1900;
    local.tm_mon = numberAt(stamp, 4, 2) - 1;
    local.tm_mday = numberAt(stamp, 6, 2);
    local.tm_hour = numberAt(stamp, 9, 2);
    local.tm_min = numberAt(stamp, 11, 2);
    local.tm_sec = numberAt(stamp, 13, 2);
    local.tm_isdst = -1;
    const std::time_t t = std::mktime(&local);
    if (t == static_cast<std::time_t>(-1)) return {};
    return format(toUtc(t));
}

bool isValidDateOrTimestamp(const std::string& text) {
    if (text.size() != 10 && text.size() != 20) return false;
    if (!digitsAt(text, 0, 4) || text[4] != '-' || !digitsAt(text, 5, 2) || text[7] != '-' ||
        !digitsAt(text, 8, 2)) {
        return false;
    }
    const int month = numberAt(text, 5, 2);
    const int day = numberAt(text, 8, 2);
    if (month < 1 || month > 12 || day < 1 || day > 31) return false;
    if (text.size() == 10) return true;

    if (text[10] != 'T' || !digitsAt(text, 11, 2) || text[13] != ':' || !digitsAt(text, 14, 2) ||
        text[16] != ':' || !digitsAt(text, 17, 2) || text[19] != 'Z') {
        return false;
    }
    return numberAt(text, 11, 2) < 24 && numberAt(text, 14, 2) < 60 && numberAt(text, 17, 2) < 61;
}

}  // namespace optinspect::database
