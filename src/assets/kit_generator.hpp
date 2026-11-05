#pragma once
// Parametric machine kits. A kit is a handful of boxes with materials, chosen by
// a seeded grammar rather than noise, because a student must be able to identify
// a photo-eye at twenty metres.
#include "core/types.hpp"
#include "world/voxel_chunk.hpp"

#include <string>
#include <vector>

namespace sc {

struct KitPiece {
    MaterialId material = 1;
    i32 x = 0;
    i32 y = 0;
    i32 z = 0;
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;
};

struct Kit {
    std::string name;
    std::vector<KitPiece> pieces;
};

// Deterministic for a given (name, seed) pair.
Kit generate_machine_kit(const std::string& name, u64 seed);

}  // namespace sc
