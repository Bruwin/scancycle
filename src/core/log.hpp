#pragma once
#include <string_view>

namespace sc {

enum class LogLevel : int { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4 };

void set_log_level(LogLevel level);
LogLevel log_level();

void log_write(LogLevel level, std::string_view message);
void log_format(LogLevel level, const char* fmt, ...);

}  // namespace sc

#define SC_TRACE(...) ::sc::log_format(::sc::LogLevel::Trace, __VA_ARGS__)
#define SC_DEBUG(...) ::sc::log_format(::sc::LogLevel::Debug, __VA_ARGS__)
#define SC_INFO(...) ::sc::log_format(::sc::LogLevel::Info, __VA_ARGS__)
#define SC_WARN(...) ::sc::log_format(::sc::LogLevel::Warn, __VA_ARGS__)
#define SC_ERROR(...) ::sc::log_format(::sc::LogLevel::Error, __VA_ARGS__)
