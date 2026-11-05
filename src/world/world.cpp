#include "world/world.hpp"

#include <array>

namespace sc {
namespace {

// Copies the 32x32 wall of `neighbour` that touches our chunk into a slab.
// `layer` is that wall's index inside the neighbour: 31 when the neighbour sits
// on the negative side of us, 0 when it sits on the positive one.
void copy_wall(const VoxelChunk& neighbour, int axis, int layer,
               std::array<MaterialId, kChunkSize * kChunkSize>& slab) {
    for (int v = 0; v < kChunkSize; ++v) {
        for (int u = 0; u < kChunkSize; ++u) {
            int p[3] = {0, 0, 0};
            p[axis] = layer;
            p[(axis + 1) % 3] = u;
            p[(axis + 2) % 3] = v;
            slab[static_cast<std::size_t>(u + kChunkSize * v)] = neighbour.at(p[0], p[1], p[2]);
        }
    }
}

}  // namespace

VoxelChunk& World::chunk_at(ChunkCoord coord) { return chunks_[coord]; }

const VoxelChunk* World::find(ChunkCoord coord) const {
    const auto it = chunks_.find(coord);
    return it == chunks_.end() ? nullptr : &it->second;
}

bool World::set_voxel(i32 world_x, i32 world_y, i32 world_z, MaterialId material) {
    const ChunkCoord coord = chunk_of(world_x, world_y, world_z);
    const int local_x = world_x & (kChunkSize - 1);
    const int local_y = world_y & (kChunkSize - 1);
    const int local_z = world_z & (kChunkSize - 1);
    return chunk_at(coord).set(local_x, local_y, local_z, material);
}

MaterialId World::voxel_at(i32 world_x, i32 world_y, i32 world_z) const {
    const ChunkCoord coord = chunk_of(world_x, world_y, world_z);
    const VoxelChunk* chunk = find(coord);
    if (chunk == nullptr) {
        return kMaterialAir;
    }
    return chunk->at(world_x & (kChunkSize - 1), world_y & (kChunkSize - 1),
                     world_z & (kChunkSize - 1));
}

std::vector<ChunkCoord> World::coords() const {
    std::vector<ChunkCoord> out;
    out.reserve(chunks_.size());
    for (const auto& entry : chunks_) {
        out.push_back(entry.first);
    }
    return out;
}

MeshData World::mesh(ChunkCoord coord) const {
    const VoxelChunk* chunk = find(coord);
    if (chunk == nullptr) {
        return MeshData{};
    }

    ChunkNeighbours neighbours;
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 0; side < 2; ++side) {
            ChunkCoord offset = coord;
            const i32 delta = (side == 0) ? 1 : -1;
            if (axis == 0) {
                offset.x += delta;
            } else if (axis == 1) {
                offset.y += delta;
            } else {
                offset.z += delta;
            }
            const VoxelChunk* neighbour = find(offset);
            if (neighbour == nullptr) {
                continue;  // absent chunks are air, and the slab is already air
            }
            copy_wall(*neighbour, axis, side == 0 ? 0 : kChunkSize - 1,
                      neighbours.slabs[static_cast<std::size_t>(axis * 2 + side)]);
        }
    }
    return greedy_mesh(*chunk, neighbours);
}

}  // namespace sc
