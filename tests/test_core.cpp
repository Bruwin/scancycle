#include "harness.hpp"

#include "core/deterministic_rng.hpp"
#include "core/fixed_step.hpp"

TEST(rng_is_reproducible) {
    sc::Pcg32 a(1234ULL, 99ULL);
    sc::Pcg32 b(1234ULL, 99ULL);
    for (int i = 0; i < 512; ++i) {
        CHECK(a.next_u32() == b.next_u32());
    }
}

TEST(rng_streams_diverge) {
    sc::Pcg32 a(1234ULL, 1ULL);
    sc::Pcg32 b(1234ULL, 2ULL);
    int differences = 0;
    for (int i = 0; i < 64; ++i) {
        if (a.next_u32() != b.next_u32()) {
            ++differences;
        }
    }
    CHECK(differences > 60);
}

TEST(rng_bounded_respects_the_bound) {
    sc::Pcg32 rng(7ULL, 3ULL);
    for (int i = 0; i < 1000; ++i) {
        CHECK(rng.bounded(10) < 10);
    }
    CHECK(rng.bounded(0) == 0);
}

TEST(fixed_step_produces_exactly_sixty_ticks_per_second) {
    sc::FixedStepClock clock(60.0);
    sc::u64 ticks = 0;
    for (int frame = 0; frame < 60; ++frame) {
        ticks += clock.advance(1.0 / 60.0, [](sc::u64) {});
    }
    CHECK(ticks == 60);
    CHECK(clock.tick_count() == 60);
}

TEST(fixed_step_clamps_a_long_frame) {
    sc::FixedStepClock clock(60.0);
    sc::u64 ticks = 0;
    ticks += clock.advance(1.0, [](sc::u64) {});
    CHECK(ticks == 8);
}
