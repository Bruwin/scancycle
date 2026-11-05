#pragma once
#include <cmath>
#include <cstdio>
#include <vector>

namespace sctest {

inline std::vector<void (*)()>& registry() {
    static std::vector<void (*)()> tests;
    return tests;
}

inline int& failure_count() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    explicit Registrar(void (*function)()) { registry().push_back(function); }
};

}  // namespace sctest

#define TEST(name)                                                              \
    static void name();                                                         \
    [[maybe_unused]] static ::sctest::Registrar registrar_##name(name);         \
    static void name()

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "  CHECK failed: %s (%s:%d)\n", #condition,    \
                         __FILE__, __LINE__);                                   \
            ++::sctest::failure_count();                                        \
        }                                                                       \
    } while (0)

#define CHECK_NEAR(a, b, epsilon)                                               \
    do {                                                                        \
        if (std::fabs((a) - (b)) > (epsilon)) {                                 \
            std::fprintf(stderr, "  CHECK_NEAR failed: %s != %s (%s:%d)\n", #a, \
                         #b, __FILE__, __LINE__);                               \
            ++::sctest::failure_count();                                        \
        }                                                                       \
    } while (0)
