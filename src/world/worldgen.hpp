#pragma once
// Procedural plant generation. Everything derives from the seed, so a teacher
// and a student always see the same factory for the same lesson.
#include "core/types.hpp"
#include "world/world.hpp"

namespace sc {

struct WorldGenParams {
    u64 seed = 1;
    i32 floor_y = 0;
    i32 chunks_x = 2;
    i32 chunks_z = 2;
    MaterialId floor_material = 1;
    MaterialId pillar_material = 2;
    u32 pillar_count = 12;
};

void generate_plant(World& world, const WorldGenParams& params);

}  // namespace sc
