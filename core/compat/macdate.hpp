// Classic Mac OS date/time helpers.
//
// Eudora's TOC files store timestamps as unsigned seconds since the Mac
// epoch, 1904-01-01 00:00:00 (the classic DateToSeconds convention).  The
// `seconds` field is UTC (the parser subtracts the message's zone offset);
// `origZone` stores the message's original UTC offset in minutes.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace eudora {

// Difference between the Mac epoch (1904-01-01) and the Unix epoch
// (1970-01-01), in seconds.
inline constexpr std::int64_t kMacToUnixEpochDelta = 2082844800;

inline std::int64_t mac_to_unix(std::uint32_t mac_seconds) {
    return static_cast<std::int64_t>(mac_seconds) - kMacToUnixEpochDelta;
}

inline std::uint32_t unix_to_mac(std::int64_t unix_seconds) {
    const std::int64_t v = unix_seconds + kMacToUnixEpochDelta;
    return v < 0 ? 0u : static_cast<std::uint32_t>(v);
}

struct DateTimeParts {
    int year = 0;   // e.g. 1999
    int month = 0;  // 1-12
    int day = 0;    // 1-31
    int hour = 0;   // 0-23
    int minute = 0; // 0-59
    int second = 0; // 0-59
};

// The classic DateToSeconds: civil date -> seconds since the Mac epoch,
// with no time zone applied (wall-clock arithmetic).
std::uint32_t mac_date_to_seconds(const DateTimeParts &parts);

// Inverse of mac_date_to_seconds.
DateTimeParts mac_seconds_to_date(std::uint32_t mac_seconds);

// Current time as Mac-epoch UTC seconds (GMTDateTime in the original).
std::uint32_t mac_now_utc();

// Local UTC offset of this machine right now, in seconds (ZoneSecs).
long local_zone_seconds();

// RFC 822 named time zone -> offset in seconds (TZName2Offset).  Returns 0
// for unknown names (including the RFC 822 military letters, which RFC 2822
// says to treat as -0000).
long tz_name_to_offset(std::string_view name);

// Numeric "+HHMM"/"-HHMM" (or named) zone -> offset in seconds (CStr2Zone).
long zone_string_to_offset(std::string_view s);

// Month abbreviation -> 1-12, or 0 (MonthNum's exact matching rules).
int month_number(std::string_view name);

} // namespace eudora
