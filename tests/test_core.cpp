#include "harness.hpp"

#include "render/camera.hpp"

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

// The cull shader reimplements this test, so proving it here is what stops the
// GPU path from disagreeing with the CPU path.
TEST(camera_frustum_keeps_what_is_in_front) {
    const sc::Mat4 view =
        sc::look_at(sc::Vec3{0.0f, 0.0f, 10.0f}, sc::Vec3{0.0f, 0.0f, 0.0f}, sc::Vec3{0.0f, 1.0f, 0.0f});
    const sc::Mat4 projection = sc::perspective(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    const sc::Mat4 view_projection = sc::multiply(projection, view);

    sc::f32 planes[6][4] = {};
    sc::extract_frustum_planes(view_projection, planes);

    const sc::f32 ahead[3] = {0.0f, 0.0f, 0.0f};
    CHECK(sc::sphere_inside_frustum(planes, ahead, 1.0f));

    const sc::f32 behind[3] = {0.0f, 0.0f, 50.0f};
    CHECK(!sc::sphere_inside_frustum(planes, behind, 1.0f));

    const sc::f32 far_left[3] = {500.0f, 0.0f, 0.0f};
    CHECK(!sc::sphere_inside_frustum(planes, far_left, 1.0f));

    const sc::f32 too_far[3] = {0.0f, 0.0f, -400.0f};
    CHECK(!sc::sphere_inside_frustum(planes, too_far, 1.0f));
}
