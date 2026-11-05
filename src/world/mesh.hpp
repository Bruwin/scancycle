#pragma once
// CPU-side mesh output of the voxel mesher. Vertices are packed to 8 bytes:
// chunk-local integer positions (0..32), a face id, a material id and an
// ambient-occlusion term. No floats, so meshes are bit-identical everywhere and
// the GPU can pull them straight out of a storage buffer.
#include "core/types.hpp"

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

class VoxelChunk;

// Greedy meshing: merges coplanar same-material faces into the largest
// possible rectangles, so a flat factory floor costs a handful of quads
// instead of one per square metre.
MeshData greedy_mesh(const VoxelChunk& chunk);

}  // namespace sc
