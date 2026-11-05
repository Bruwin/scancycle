#pragma once
// Sits between the world and the RHI. Chunk meshes arrive from worker threads
// and are uploaded under a per-frame budget, so a burst of remeshed chunks can
// never stall a frame.
#include "core/types.hpp"
#include "render/rhi.hpp"
#include "world/mesh.hpp"

#include <vector>

namespace sc {

class VoxelRenderer {
public:
    explicit VoxelRenderer(RhiDevice& device) : device_(device) {}

    void enqueue_chunk(u32 chunk_x, u32 chunk_y, u32 chunk_z, MeshData mesh);

    // Uploads at most `budget` queued chunks. Returns how many were uploaded.
    u32 upload_pending(u32 budget);

    u32 pending_count() const { return static_cast<u32>(pending_.size()); }
    DrawStatistics statistics() const { return device_.statistics(); }

private:
    struct Pending {
        u32 x = 0;
        u32 y = 0;
        u32 z = 0;
        MeshData mesh;
    };

    RhiDevice& device_;
    std::vector<Pending> pending_;
    std::vector<GpuMeshHandle> live_;
};

}  // namespace sc
