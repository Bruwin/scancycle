#pragma once
// Chunk rendering: one storage buffer of packed vertices, one of indices, a
// compute pass that compacts visible chunks into indirect draw commands, and a
// single vkCmdDrawIndexedIndirectCount for the whole world.
//
// The CPU only ever appends to the arena and flags it dirty; after upload it does
// not walk the chunk list again.
#include "core/types.hpp"
#include "render/mesh_arena.hpp"
#include "render/rhi.hpp"
#include "render/vulkan/vk_buffer.hpp"

#include <string>
#include <vector>

namespace sc::vk {

class ChunkRenderer {
public:
    ChunkRenderer() = default;
    ~ChunkRenderer() = default;

    ChunkRenderer(const ChunkRenderer&) = delete;
    ChunkRenderer& operator=(const ChunkRenderer&) = delete;

    bool initialize(VkPhysicalDevice physical, VkDevice device, VkCommandPool command_pool,
                    VkQueue queue, VkFormat color_format, VkFormat depth_format,
                    const std::string& shader_directory, std::string& error);

    void destroy();

    // Appends a chunk mesh at a world-space origin and returns a stable handle.
    GpuMeshHandle add(const MeshData& mesh, f32 origin_x, f32 origin_y, f32 origin_z);
    void remove(GpuMeshHandle handle);

    DrawStatistics statistics() const { return stats_; }

    // Uploads if needed and dispatches the cull. Must run outside a rendering
    // scope: a compute dispatch cannot sit inside one.
    void prepare(VkCommandBuffer cmd, const FrameConstants& constants, VkExtent2D extent);

    // Binds the chunk pipeline and issues the single indirect draw. Must be called
    // inside a dynamic-rendering scope.
    void draw(VkCommandBuffer cmd, VkExtent2D extent);

private:
    struct ChunkRecord {
        u32 id = 0;
        f32 origin[3] = {0.0f, 0.0f, 0.0f};
        u32 first_index = 0;
        u32 index_count = 0;
        u32 first_vertex = 0;
        bool live = false;
    };

    bool create_layout(std::string& error);
    bool create_pipelines(const std::string& shader_directory, VkFormat color_format,
                          VkFormat depth_format, std::string& error);
    bool ensure_capacity(u32 chunk_count);
    bool upload_arena();
    void write_frame_block(const FrameConstants& constants);

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline_ = VK_NULL_HANDLE;
    VkPipeline compute_pipeline_ = VK_NULL_HANDLE;

    Buffer vertex_buffer_;
    Buffer index_buffer_;
    Buffer chunk_buffer_;
    Buffer range_buffer_;
    Buffer draw_buffer_;
    Buffer count_buffer_;
    Buffer frame_buffer_;

    MeshArena arena_;
    std::vector<ChunkRecord> chunks_;
    u32 capacity_ = 0;
    bool arena_dirty_ = true;
    u32 next_mesh_id_ = 0;
    DrawStatistics stats_;
};

}  // namespace sc::vk
