#include "render/mesh_arena.hpp"

namespace sc {

MeshRange MeshArena::append(const MeshData& mesh) {
    MeshRange range;
    range.first_vertex = static_cast<u32>(vertices_.size());
    range.vertex_count = static_cast<u32>(mesh.vertices.size());
    range.first_index = static_cast<u32>(indices_.size());
    range.index_count = static_cast<u32>(mesh.indices.size());

    vertices_.insert(vertices_.end(), mesh.vertices.begin(), mesh.vertices.end());
    for (u32 index : mesh.indices) {
        indices_.push_back(index + range.first_vertex);
    }

    if (!mesh.empty()) {
        ++chunk_count_;
    }
    return range;
}

void MeshArena::clear() {
    vertices_.clear();
    indices_.clear();
    chunk_count_ = 0;
}

}  // namespace sc
