#include "harness.hpp"

#include "render/rhi.hpp"
#include "render/voxel_renderer.hpp"
#include "world/voxel_chunk.hpp"
#include "world/world.hpp"

#include <memory>

namespace {

// A three by three floor aligned to the chunk grid, so the chunks that exist are
// exactly (0..2, 0, 0..2) and a test can talk about them by name.
sc::World make_chunk_floor() {
    sc::World world;
    for (int cz = 0; cz < 3; ++cz) {
        for (int cx = 0; cx < 3; ++cx) {
            world.set_voxel(cx * sc::kChunkSize + 4, 0, cz * sc::kChunkSize + 4, 1);
        }
    }
    return world;
}

}  // namespace

TEST(streaming_requests_the_chunks_around_the_camera) {
    const sc::World world = make_chunk_floor();
    const std::unique_ptr<sc::RhiDevice> device = sc::create_null_rhi_device();
    sc::VoxelRenderer renderer(*device);

    CHECK(renderer.stream(world, {1, 0, 1}, 1) == 9);
    CHECK(renderer.pending_count() == 9);
    CHECK(renderer.upload_pending(world, 100) == 9);
    CHECK(renderer.resident_count() == 9);
    CHECK(renderer.pending_count() == 0);

    device->draw_frame();
    CHECK(renderer.statistics().live_meshes == 9);
}

TEST(streaming_spends_its_budget_and_leaves_the_rest_queued) {
    const sc::World world = make_chunk_floor();
    const std::unique_ptr<sc::RhiDevice> device = sc::create_null_rhi_device();
    sc::VoxelRenderer renderer(*device);

    renderer.stream(world, {1, 0, 1}, 1);
    CHECK(renderer.upload_pending(world, 2) == 2);
    CHECK(renderer.resident_count() == 2);
    CHECK(renderer.pending_count() == 7);
}

TEST(streaming_releases_chunks_the_radius_no_longer_covers) {
    const sc::World world = make_chunk_floor();
    const std::unique_ptr<sc::RhiDevice> device = sc::create_null_rhi_device();
    sc::VoxelRenderer renderer(*device);

    renderer.stream(world, {1, 0, 1}, 1);
    renderer.upload_pending(world, 100);
    CHECK(renderer.resident_count() == 9);

    // Far enough that every resident chunk is past the radius plus its slack.
    CHECK(renderer.stream(world, {20, 0, 20}, 1) == 0);
    CHECK(renderer.resident_count() == 0);
}

TEST(streaming_does_not_request_the_same_chunk_twice) {
    const sc::World world = make_chunk_floor();
    const std::unique_ptr<sc::RhiDevice> device = sc::create_null_rhi_device();
    sc::VoxelRenderer renderer(*device);

    CHECK(renderer.stream(world, {1, 0, 1}, 1) == 9);
    CHECK(renderer.stream(world, {1, 0, 1}, 1) == 0);
    CHECK(renderer.pending_count() == 9);
}
