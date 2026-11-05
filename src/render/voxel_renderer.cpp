#include "render/voxel_renderer.hpp"

#include <utility>

namespace sc {

void VoxelRenderer::enqueue_chunk(u32 chunk_x, u32 chunk_y, u32 chunk_z, MeshData mesh) {
    pending_.push_back(Pending{chunk_x, chunk_y, chunk_z, std::move(mesh)});
}

u32 VoxelRenderer::upload_pending(u32 budget) {
    u32 uploaded = 0;
    while (!pending_.empty() && uploaded < budget) {
        Pending pending = std::move(pending_.back());
        pending_.pop_back();
        live_.push_back(device_.upload_chunk_mesh(pending.x, pending.y, pending.z, pending.mesh));
        ++uploaded;
    }
    return uploaded;
}

}  // namespace sc
