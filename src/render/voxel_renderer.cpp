#include "render/voxel_renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <utility>

namespace sc {
namespace {

// Chebyshev distance, because the radius is a cube of chunks: a sphere would
// drag in a diagonal ring the frustum rarely needs and pay to mesh it.
i32 chunk_distance(ChunkCoord a, ChunkCoord b) {
    return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

bool same_coord(ChunkCoord a, ChunkCoord b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

}  // namespace

void VoxelRenderer::enqueue_chunk(i32 chunk_x, i32 chunk_y, i32 chunk_z, MeshData mesh) {
    Pending pending;
    pending.coord = ChunkCoord{chunk_x, chunk_y, chunk_z};
    pending.mesh = std::move(mesh);
    pending.meshed = true;
    pending_.push_back(std::move(pending));
}

bool VoxelRenderer::is_pending(ChunkCoord coord) const {
    for (const Pending& pending : pending_) {
        if (same_coord(pending.coord, coord)) {
            return true;
        }
    }
    return false;
}

bool VoxelRenderer::is_resident(ChunkCoord coord) const {
    for (const Resident& resident : resident_) {
        if (same_coord(resident.coord, coord)) {
            return true;
        }
    }
    return false;
}

u32 VoxelRenderer::stream(const World& world, ChunkCoord center, u32 radius) {
    // Release before requesting, so the arena gives up the geometry behind the
    // camera before it pays for the geometry in front of it. One chunk of slack
    // past the radius stops a camera sitting on a boundary from dropping and
    // re-meshing the same chunk every frame.
    for (std::size_t i = resident_.size(); i-- > 0;) {
        if (chunk_distance(resident_[i].coord, center) > static_cast<i32>(radius) + 1) {
            device_.release_mesh(resident_[i].handle);
            resident_.erase(resident_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    const i32 reach = static_cast<i32>(radius);
    std::vector<ChunkCoord> wanted;
    for (i32 dz = -reach; dz <= reach; ++dz) {
        for (i32 dy = -reach; dy <= reach; ++dy) {
            for (i32 dx = -reach; dx <= reach; ++dx) {
                const ChunkCoord coord{center.x + dx, center.y + dy, center.z + dz};
                if (chunk_distance(coord, center) > reach) {
                    continue;
                }
                if (world.find(coord) == nullptr) {
                    continue;  // nothing to draw there yet
                }
                if (is_resident(coord) || is_pending(coord)) {
                    continue;
                }
                wanted.push_back(coord);
            }
        }
    }

    // Nearest first, with the coordinates breaking ties, so the upload order is
    // identical on every machine and a captured frame stays reproducible.
    std::sort(wanted.begin(), wanted.end(), [center](ChunkCoord a, ChunkCoord b) {
        const i32 da = chunk_distance(a, center);
        const i32 db = chunk_distance(b, center);
        if (da != db) {
            return da < db;
        }
        if (a.y != b.y) {
            return a.y < b.y;
        }
        if (a.z != b.z) {
            return a.z < b.z;
        }
        return a.x < b.x;
    });

    for (const ChunkCoord& coord : wanted) {
        Pending pending;
        pending.coord = coord;
        pending_.push_back(std::move(pending));
    }
    return static_cast<u32>(wanted.size());
}

u32 VoxelRenderer::upload_pending(const World& world, u32 budget) {
    u32 uploaded = 0;
    while (!pending_.empty() && uploaded < budget) {
        Pending pending = std::move(pending_.front());
        pending_.pop_front();
        const MeshData mesh = pending.meshed ? std::move(pending.mesh) : world.mesh(pending.coord);
        resident_.push_back(
            Resident{pending.coord, device_.upload_chunk_mesh(pending.coord.x, pending.coord.y,
                                                             pending.coord.z, mesh)});
        ++uploaded;
    }
    return uploaded;
}

}  // namespace sc
