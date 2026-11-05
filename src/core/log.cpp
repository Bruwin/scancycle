#include "core/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace sc {
namespace {

LogLevel g_level = LogLevel::Info;
std::mutex g_mutex;

const char* level_tag(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warn:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
    }
    return "?????";
}

bool enabled(LogLevel level) { return static_cast<int>(level) >= static_cast<int>(g_level); }

}  // namespace

void set_log_level(LogLevel level) { g_level = level; }

LogLevel log_level() { return g_level; }

void log_write(LogLevel level, std::string_view message) {
    if (!enabled(level)) {
        return;
    }
    const std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "[%s] %.*s\n", level_tag(level), static_cast<int>(message.size()),
                 message.data());
    std::fflush(stderr);
}

void log_format(LogLevel level, const char* fmt, ...) {
    if (!enabled(level)) {
        return;
    }
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_write(level, std::string_view(buffer));
}

}  // namespace sc
