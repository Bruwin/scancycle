#include "render/rhi.hpp"

#include <vector>

namespace sc {
namespace {

struct NullMeshRecord {
    u32 id = 0;
    u32 quads = 0;
};

class NullRhiDevice final : public RhiDevice {
public:
    const char* backend_name() const override { return "null"; }

    GpuMeshHandle upload_chunk_mesh(i32 chunk_x, i32 chunk_y, i32 chunk_z,
                                    const MeshData& mesh) override {
        (void)chunk_x;
        (void)chunk_y;
        (void)chunk_z;
        const u32 id = ++next_id_;
        meshes_.push_back(NullMeshRecord{id, static_cast<u32>(mesh.quad_count())});
        return GpuMeshHandle{id};
    }

    void release_mesh(GpuMeshHandle handle) override {
        for (std::size_t i = 0; i < meshes_.size(); ++i) {
            if (meshes_[i].id == handle.id) {
                meshes_.erase(meshes_.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    }

    void begin_frame(const FrameConstants& constants) override {
        (void)constants;
        stats_.quad_count = 0;
    }

    void draw_frame() override {
        stats_.quad_count = 0;
        stats_.live_meshes = static_cast<u32>(meshes_.size());
        for (const NullMeshRecord& mesh : meshes_) {
            stats_.quad_count += mesh.quads;
        }
        // The real backend issues one indirect draw for the whole world.
        stats_.draw_calls = meshes_.empty() ? 0u : 1u;
    }

    void end_frame() override {}

    DrawStatistics statistics() const override { return stats_; }

private:
    std::vector<NullMeshRecord> meshes_;
    DrawStatistics stats_;
    u32 next_id_ = 0;
};

}  // namespace

std::unique_ptr<RhiDevice> create_null_rhi_device() { return std::make_unique<NullRhiDevice>(); }

}  // namespace sc
