#pragma once
// CPU-side mesh output of the voxel mesher. Vertices are packed to 8 bytes:
// chunk-local integer positions (0..32), a face id, a material id and an
// ambient-occlusion term. No floats, so meshes are bit-identical everywhere and
// the GPU can pull them straight out of a storage buffer.
#include "core/types.hpp"
#include "world/voxel_chunk.hpp"

#include <array>
#include <vector>

namespace sc {

enum FaceId : u8 {
    kFacePosX = 0,
    kFaceNegX = 1,
    kFacePosY = 2,
    kFaceNegY = 3,
    kFacePosZ = 4,
    kFaceNegZ = 5,
};

struct PackedVertex {
    u8 x;
    u8 y;
    u8 z;
    u8 face;
    u8 ao;
    u8 pad;
    u16 material;
};

static_assert(sizeof(PackedVertex) == 8, "PackedVertex must stay 8 bytes");

struct MeshData {
    std::vector<PackedVertex> vertices;
    std::vector<u32> indices;

    std::size_t quad_count() const { return vertices.size() / 4; }
    bool empty() const { return vertices.empty(); }
    void clear();
};

// The voxels just outside a chunk's six walls. The mesher consults them only
// when it looks one step past an edge, which is exactly where a face shared by
// two solid chunks used to be drawn twice and z-fight.
//
// Slabs are indexed like FaceId (axis * 2, positive direction first). Inside a
// slab the index is (u + kChunkSize * v), with u on (axis + 1) % 3 and v on
// (axis + 2) % 3 - the mesher's own plane axes, so writer and reader cannot
// disagree about the order. A neighbour that was never filled stays zeroed,
// which is air, so meshing a chunk on its own behaves exactly as before.
struct ChunkNeighbours {
    std::array<std::array<MaterialId, kChunkSize * kChunkSize>, 6> slabs{};
};

// Greedy meshing: merges coplanar same-material faces into the largest
// possible rectangles, so a flat factory floor costs a handful of quads
// instead of one per square metre.
MeshData greedy_mesh(const VoxelChunk& chunk);
MeshData greedy_mesh(const VoxelChunk& chunk, const ChunkNeighbours& neighbours);

}  // namespace sc
