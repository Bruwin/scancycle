#pragma once
// Assertions are for programming errors, not for input validation. They fire in
// every build configuration: a silently corrupt simulation is worse than a
// crash, because the grading results would be wrong instead of obviously broken.
#include "core/log.hpp"

#include <cstdlib>

namespace sc {

[[noreturn]] inline void assertion_failed(const char* expression, const char* file, int line) {
    log_format(LogLevel::Error, "assertion failed: %s (%s:%d)", expression, file, line);
    std::abort();
}

}  // namespace sc

#define SC_ASSERT(expression) \
    ((expression) ? static_cast<void>(0) : ::sc::assertion_failed(#expression, __FILE__, __LINE__))
