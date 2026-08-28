#include "compat/macdate.hpp"

#include <cctype>
#include <cstdlib>
#include <ctime>

namespace eudora {

// Days from civil date to days since 1970-01-01 (Howard Hinnant's algorithm).
static std::int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2u) / 5u +
                         static_cast<unsigned>(d) - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

static void civil_from_days(std::int64_t z, int &y, int &m, int &d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t yy = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp + (mp < 10 ? 3 : -9));
    y = static_cast<int>(yy + (m <= 2));
}

std::uint32_t mac_date_to_seconds(const DateTimeParts &p) {
    const std::int64_t days = days_from_civil(p.year, p.month, p.day);
    const std::int64_t unix_secs =
        days * 86400 + p.hour * 3600 + p.minute * 60 + p.second;
    const std::int64_t mac = unix_secs + kMacToUnixEpochDelta;
    return mac < 0 ? 0u : static_cast<std::uint32_t>(mac);
}

DateTimeParts mac_seconds_to_date(std::uint32_t mac_seconds) {
    DateTimeParts p;
    const std::int64_t unix_secs =
        static_cast<std::int64_t>(mac_seconds) - kMacToUnixEpochDelta;
    std::int64_t days = unix_secs / 86400;
    std::int64_t rem = unix_secs % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    civil_from_days(days, p.year, p.month, p.day);
    p.hour = static_cast<int>(rem / 3600);
    p.minute = static_cast<int>((rem % 3600) / 60);
    p.second = static_cast<int>(rem % 60);
    return p;
}

std::uint32_t mac_now_utc() {
    return unix_to_mac(static_cast<std::int64_t>(std::time(nullptr)));
}

long local_zone_seconds() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    std::tm utc{};
#if defined(_WIN32)
    localtime_s(&local, &now);
    gmtime_s(&utc, &now);
#else
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
#endif
    const std::int64_t l = days_from_civil(local.tm_year + 1900, local.tm_mon + 1,
                                           local.tm_mday) * 86400 +
                           local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    const std::int64_t u = days_from_civil(utc.tm_year + 1900, utc.tm_mon + 1,
                                           utc.tm_mday) * 86400 +
                           utc.tm_hour * 3600 + utc.tm_min * 60 + utc.tm_sec;
    return static_cast<long>(l - u);
}

long tz_name_to_offset(std::string_view name) {
    // Named zones from the legacy 'zon#' resource (id 1001, read by
    // TZName2Offset, util.c:2357).  The original shipped all of these, not
    // just the North-American set — a Date: … JST header must resolve to
    // +9h, not be treated as local time.  (Legacy truncated a few of these
    // offsets through a `short index`, yielding nonsense values for the
    // far-east zones; that is a bug we deliberately do not reproduce, so we
    // use the real offsets in minutes.)
    struct Zone {
        const char *name;
        long minutes;
    };
    static constexpr Zone kZones[] = {
        {"UT", 0},      {"UTC", 0},    {"GMT", 0},
        {"EST", -300},  {"EDT", -240}, {"CST", -360}, {"CDT", -300},
        {"MST", -420},  {"MDT", -360}, {"PST", -480}, {"PDT", -420},
        {"HST", -600},                                       // Hawaii
        {"MET", 60},    {"MEZ", 60},   {"MET DST", 120},     // Central Europe
        {"BST", 60},    {"HOE", 60},   {"DNT", 60},          // +1 zones
        {"IDT", 180},                                        // Israel DST
        {"SST", 480},                                        // +8
        {"WST", 540},   {"JST", 540},  {"KST", 540},         // +9
        {"AEST", 600},                                       // +10
        {"NZD", 780},                                        // +13
    };
    std::string upper;
    for (char c : name)
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const auto &z : kZones)
        if (upper == z.name)
            return z.minutes * 60;
    return 0;
}

long zone_string_to_offset(std::string_view s) {
    // Port of CStr2Zone (buildtoc.c): numeric "+HHMM"/"-HHMM" preferred,
    // named zones otherwise; implausible numeric zones fall back to the
    // local zone.
    long numeric = 0;
    {
        std::string tmp(s);
        numeric = std::strtol(tmp.c_str(), nullptr, 10);
    }
    if (numeric != 0) {
        if (numeric > 2400 || numeric < -2400)
            return local_zone_seconds();
        long v = numeric < 0 ? -numeric : numeric;
        v = 60 * ((v / 100) * 60 + v % 100);
        return numeric < 0 ? -v : v;
    }
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    if (i + 1 < s.size() && (s[i] == '-' || s[i] == '+') && s[i + 1] == '0')
        return 0; // it really was zero
    return tz_name_to_offset(s.substr(i));
}

int month_number(std::string_view name) {
    if (name.size() < 3)
        return 0;
    char m[3];
    for (int i = 0; i < 3; ++i)
        m[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    switch (m[0]) {
    case 'j': return m[1] == 'a' ? 1 : (m[2] == 'n' ? 6 : 7);
    case 'f': return 2;
    case 'm': return m[2] == 'r' ? 3 : 5;
    case 'a': return m[1] == 'p' ? 4 : 8;
    case 's': return 9;
    case 'o': return 10;
    case 'n': return 11;
    case 'd': return 12;
    default: return 0;
    }
}

} // namespace eudora
