#pragma once
// Sits between the world and the RHI. Chunk meshes arrive from worker threads
// and are uploaded under a per-frame budget, so a burst of remeshed chunks can
// never stall a frame.
//
// It also decides what is resident: a camera position and a radius go in, and
// the chunks that belong on the GPU come out. Meshing waits for the budgeted
// upload for the same reason - a ring of new chunks must not cost a frame.
#include "core/types.hpp"
#include "render/rhi.hpp"
#include "world/mesh.hpp"
#include "world/world.hpp"

#include <deque>
#include <vector>

namespace sc {

class VoxelRenderer {
public:
    explicit VoxelRenderer(RhiDevice& device) : device_(device) {}

    // Queues a mesh the caller already has, ahead of anything stream() found.
    void enqueue_chunk(i32 chunk_x, i32 chunk_y, i32 chunk_z, MeshData mesh);

    // Requests every chunk within `radius` chunks of `center` that the world
    // actually has, nearest first, and releases the ones the radius no longer
    // covers. Returns how many were newly queued.
    u32 stream(const World& world, ChunkCoord center, u32 radius);

    // Meshes and uploads at most `budget` queued chunks, nearest first.
    u32 upload_pending(const World& world, u32 budget);

    u32 pending_count() const { return static_cast<u32>(pending_.size()); }
    u32 resident_count() const { return static_cast<u32>(resident_.size()); }
    DrawStatistics statistics() const { return device_.statistics(); }

private:
    struct Pending {
        ChunkCoord coord;
        MeshData mesh;
        bool meshed = false;
    };

    struct Resident {
        ChunkCoord coord;
        GpuMeshHandle handle;
    };

    bool is_pending(ChunkCoord coord) const;
    bool is_resident(ChunkCoord coord) const;

    RhiDevice& device_;
    std::deque<Pending> pending_;
    std::vector<Resident> resident_;
};

}  // namespace sc
