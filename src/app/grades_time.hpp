#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace sc::app {

// One stamp format for the whole portal: UTC, minute resolution, so a classroom
// log compares across terms, machines and time zones. Header-only because there
// is no state to keep and every caller wants the same answer.
inline std::string format_utc_minutes(std::int64_t seconds) {
    const std::time_t when = static_cast<std::time_t>(seconds);
    std::tm parts{};
#if defined(_WIN32)
    gmtime_s(&parts, &when);
#else
    gmtime_r(&when, &parts);
#endif
    char text[32];
    if (std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &parts) == 0) {
        return "unknown";
    }
    return std::string(text);
}

}  // namespace sc::app
