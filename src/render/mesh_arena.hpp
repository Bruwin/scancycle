#pragma once
// Packs every chunk mesh into two contiguous cpu-side buffers. The GPU path
// uploads these once and pulls vertices from storage buffers, which is why
// there is no per-chunk vertex buffer here.
#include "core/types.hpp"
#include "world/mesh.hpp"

#include <span>
#include <vector>

namespace sc {

struct MeshRange {
    u32 first_vertex = 0;
    u32 vertex_count = 0;
    u32 first_index = 0;
    u32 index_count = 0;
};

class MeshArena {
public:
    MeshRange append(const MeshData& mesh);
    void clear();

    std::span<const PackedVertex> vertices() const { return vertices_; }
    std::span<const u32> indices() const { return indices_; }
    std::size_t chunk_count() const { return chunk_count_; }
    std::size_t byte_size() const {
        return vertices_.size() * sizeof(PackedVertex) + indices_.size() * sizeof(u32);
    }

private:
    std::vector<PackedVertex> vertices_;
    std::vector<u32> indices_;
    std::size_t chunk_count_ = 0;
};

}  // namespace sc
