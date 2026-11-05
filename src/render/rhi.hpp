#pragma once
// Render hardware interface. The Vulkan 1.3 backend implements this in M2; the
// null device keeps every simulation, editor and grading test GPU-free.
#include "core/types.hpp"
#include "world/mesh.hpp"

#include <memory>
#include <vector>

namespace sc {

struct FrameConstants {
    f32 view_projection[16] = {};  // column-major
    f32 time_seconds = 0.0f;
    u32 viewport_width = 0;
    u32 viewport_height = 0;
};

struct GpuMeshHandle {
    u32 id = 0;
    static constexpr u32 kInvalid = 0;

    bool valid() const { return id != kInvalid; }
};

inline bool operator==(GpuMeshHandle a, GpuMeshHandle b) { return a.id == b.id; }

struct DrawStatistics {
    u32 live_meshes = 0;
    u32 quad_count = 0;
    u32 draw_calls = 0;
};

class RhiDevice {
public:
    virtual ~RhiDevice() = default;

    virtual const char* backend_name() const = 0;

    // One contiguous upload per chunk. Handles are stable until released.
    virtual GpuMeshHandle upload_chunk_mesh(i32 chunk_x, i32 chunk_y, i32 chunk_z,
                                            const MeshData& mesh) = 0;
    virtual void release_mesh(GpuMeshHandle handle) = 0;

    virtual void begin_frame(const FrameConstants& constants) = 0;
    virtual void draw_frame() = 0;
    virtual void end_frame() = 0;

    virtual DrawStatistics statistics() const = 0;

    // Driver complaints the backend observed. Always zero without
    // validation; the viewer turns it into a non-zero exit so CI fails
    // loudly instead of rendering something subtly wrong.
    virtual u32 validation_errors() const { return 0; }

    // Whether validation_errors() is live. A zero count means nothing if the
    // layer never loaded, so the viewer prints this next to the summary.
    virtual bool validation_active() const { return false; }

    // Copies the frame captured this run as tightly packed RGBA8. False when the
    // backend has nothing to hand over, so the null device stays honest.
    virtual bool read_back(std::vector<u8>& pixels, u32& width, u32& height) {
        (void)pixels;
        (void)width;
        (void)height;
        return false;
    }
};

std::unique_ptr<RhiDevice> create_null_rhi_device();

}  // namespace sc
