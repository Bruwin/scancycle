#include "world/worldgen.hpp"

#include "core/deterministic_rng.hpp"

namespace sc {

void generate_plant(World& world, const WorldGenParams& params) {
    Pcg32 rng(params.seed, 0x5ca11cULL);

    const i32 width = params.chunks_x * kChunkSize;
    const i32 depth = params.chunks_z * kChunkSize;
    const i32 base_x = -(width / 2);
    const i32 base_z = -(depth / 2);

    for (i32 z = 0; z < depth; ++z) {
        for (i32 x = 0; x < width; ++x) {
            world.set_voxel(base_x + x, params.floor_y, base_z + z, params.floor_material);
        }
    }

    for (u32 pillar = 0; pillar < params.pillar_count; ++pillar) {
        const i32 x = base_x + static_cast<i32>(rng.bounded(static_cast<u32>(width)));
        const i32 z = base_z + static_cast<i32>(rng.bounded(static_cast<u32>(depth)));
        const i32 height = 1 + static_cast<i32>(rng.bounded(6));
        for (i32 y = 1; y <= height; ++y) {
            world.set_voxel(x, params.floor_y + y, z, params.pillar_material);
        }
    }
}

}  // namespace sc
