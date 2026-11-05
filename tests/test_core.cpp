#include "harness.hpp"

#include "core/image.hpp"

#include <cstdio>

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

// The golden-image path is only as trustworthy as this module, and this module is
// the part CI can check without a GPU.
TEST(thumbnail_box_filters_and_swizzles_bgra) {
    const sc::u8 rgba[16] = {
        10, 20, 30, 255, 30, 40, 50, 255,
        50, 60, 70, 255, 70, 80, 90, 255,
    };
    const sc::Thumbnail thumb = sc::make_thumbnail(rgba, 2, 2, 1, 1, false);
    CHECK(thumb.width == 1);
    CHECK(thumb.height == 1);
    CHECK(thumb.rgb.size() == 3);
    CHECK(thumb.rgb[0] == 40);
    CHECK(thumb.rgb[1] == 50);
    CHECK(thumb.rgb[2] == 60);

    const sc::Thumbnail swapped = sc::make_thumbnail(rgba, 2, 2, 1, 1, true);
    CHECK(swapped.rgb[0] == 60);
    CHECK(swapped.rgb[2] == 40);
}

TEST(thumbnail_rejects_input_smaller_than_it_claims) {
    const sc::u8 tiny[4] = {1, 2, 3, 4};
    CHECK(sc::make_thumbnail(tiny, 4, 4, 2, 2, false).empty());
}

TEST(identical_thumbnails_differ_by_nothing) {
    sc::Thumbnail image;
    image.width = 2;
    image.height = 1;
    image.rgb = {1, 2, 3, 250, 251, 252};
    const sc::ImageDifference diff = sc::compare_thumbnails(image, image);
    CHECK(diff.max_channel_delta == 0);
    CHECK(diff.mean_channel_delta == 0.0);
    CHECK(diff.changed_pixels == 0);
    CHECK(diff.within(0));
}

TEST(golden_comparison_separates_driver_drift_from_a_regression) {
    sc::Thumbnail expected;
    expected.width = 2;
    expected.height = 1;
    expected.rgb = {10, 20, 30, 40, 50, 60};

    // Drift is judged on the mean: a uniform few-per-channel shift is what a
    // different driver produces, and it must pass at its own magnitude while
    // failing one step tighter.
    sc::Thumbnail drift = expected;
    for (std::size_t i = 0; i < drift.rgb.size(); ++i) {
        drift.rgb[i] = static_cast<sc::u8>(drift.rgb[i] + 4);
    }
    const sc::ImageDifference small = sc::compare_thumbnails(drift, expected);
    CHECK(small.max_channel_delta == 4);
    CHECK(small.mean_channel_delta > 3.9);
    CHECK(small.within(4));
    CHECK(!small.within(3));

    // One channel moving barely dents the mean, which is the point of averaging.
    sc::Thumbnail speck = expected;
    speck.rgb[0] = 14;
    CHECK(sc::compare_thumbnails(speck, expected).within(3));

    sc::Thumbnail wrong = expected;
    wrong.rgb = {200, 210, 220, 230, 240, 250};
    CHECK(!sc::compare_thumbnails(wrong, expected).within(8));

    sc::Thumbnail resized = expected;
    resized.width = 3;
    CHECK(!sc::compare_thumbnails(resized, expected).within(255));
}

TEST(ppm_round_trips_through_a_file) {
    sc::Thumbnail image;
    image.width = 3;
    image.height = 2;
    image.rgb = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};

    const std::string path = "sc_ppm_round_trip.ppm";
    CHECK(sc::write_ppm(path, image));

    sc::Thumbnail loaded;
    CHECK(sc::read_ppm(path, loaded));
    CHECK(loaded.width == 3);
    CHECK(loaded.height == 2);
    CHECK(loaded.rgb == image.rgb);

    std::remove(path.c_str());
    sc::Thumbnail missing;
    CHECK(!sc::read_ppm(path, missing));
}
