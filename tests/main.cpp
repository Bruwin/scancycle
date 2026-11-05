#include "harness.hpp"

#include <cstdio>

int main() {
    int failed_tests = 0;
    int total_tests = 0;

    for (void (*test)() : sctest::registry()) {
        const int before = sctest::failure_count();
        test();
        ++total_tests;
        if (sctest::failure_count() != before) {
            ++failed_tests;
        }
    }

    std::printf("[sc_tests] %d test(s), %d failed, %d assertion failure(s)\n", total_tests,
                failed_tests, sctest::failure_count());
    return failed_tests == 0 ? 0 : 1;
}
