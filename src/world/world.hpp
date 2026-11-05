#pragma once
// Dense-ish chunk container. Coordinates are signed so a factory can extend in
// any direction from the origin. The shift/mask pair is floor division for
// negative coordinates, which is what "chunk -1, local 31" must mean.
#include "core/types.hpp"
#include "world/mesh.hpp"
#include "world/voxel_chunk.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace sc {

struct ChunkCoord {
    i32 x = 0;
    i32 y = 0;
    i32 z = 0;
};

inline bool operator==(const ChunkCoord& a, const ChunkCoord& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& coord) const {
        // 3 large primes; collisions are irrelevant next to the hash cost.
        return static_cast<std::size_t>((static_cast<u64>(static_cast<u32>(coord.x)) * 73856093ULL) ^
                                        (static_cast<u64>(static_cast<u32>(coord.y)) * 19349663ULL) ^
                                        (static_cast<u64>(static_cast<u32>(coord.z)) * 83492791ULL));
    }
};

inline constexpr int kChunkShift = 5;  // log2(kChunkSize)
static_assert((1 << kChunkShift) == kChunkSize, "kChunkShift must match kChunkSize");

inline ChunkCoord chunk_of(i32 world_x, i32 world_y, i32 world_z) {
    return ChunkCoord{world_x >> kChunkShift, world_y >> kChunkShift, world_z >> kChunkShift};
}

class World {
public:
    VoxelChunk& chunk_at(ChunkCoord coord);
    const VoxelChunk* find(ChunkCoord coord) const;

    bool set_voxel(i32 world_x, i32 world_y, i32 world_z, MaterialId material);
    MaterialId voxel_at(i32 world_x, i32 world_y, i32 world_z) const;

    std::size_t chunk_count() const { return chunks_.size(); }
    std::vector<ChunkCoord> coords() const;

    // Meshes one chunk with its neighbours' walls folded in, so a face shared
    // with a solid neighbour is not emitted. A neighbour that was never loaded
    // counts as air, which is also what the edge of the loaded world is.
    MeshData mesh(ChunkCoord coord) const;

private:
    std::unordered_map<ChunkCoord, VoxelChunk, ChunkCoordHash> chunks_;
};

}  // namespace sc
