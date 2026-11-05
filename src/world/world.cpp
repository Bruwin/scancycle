#include "world/world.hpp"

namespace sc {

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
    return greedy_mesh(*chunk);
}

}  // namespace sc
