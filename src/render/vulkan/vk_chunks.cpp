#include "render/vulkan/vk_chunks.hpp"

#include "render/camera.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace sc::vk {
namespace {

constexpr u32 kMaxDraws = 65536;
constexpr f32 kChunkExtent = 32.0f;
// Half the diagonal of a 32^3 chunk, so a sphere test never clips a corner.
constexpr f32 kChunkRadius = 27.7f;
constexpr f32 kMaxDistance = 320.0f;

// Mirrors the FrameBlock in every shader. std140-compatible by construction.
struct FrameBlock {
    f32 view_projection[16];
    f32 camera_position[4];
    f32 frustum_planes[6][4];
    u32 chunk_count;
    u32 max_chunks;
    f32 max_distance;
    f32 pad0;
};

struct ChunkGpu {
    f32 origin_radius[4];
};

struct RangeGpu {
    u32 first_index;
    u32 index_count;
    u32 first_vertex;
    u32 pad;
};

std::vector<char> read_binary(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }
    std::vector<char> data(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(data.data(), size);
    return data;
}

VkShaderModule create_module(VkDevice device, const std::vector<char>& code, const char* name,
                             std::string& error) {
    if (code.empty() || code.size() % 4 != 0) {
        error = std::string("shader '") + name + "' is missing or not valid SPIR-V";
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const u32*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        error = std::string("vkCreateShaderModule failed for '") + name + "'";
        return VK_NULL_HANDLE;
    }
    return module;
}

}  // namespace

bool ChunkRenderer::create_layout(std::string& error) {
    VkDescriptorSetLayoutBinding bindings[6]{};
    const VkShaderStageFlags all = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                   VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = all;

    const VkShaderStageFlags compute_only = VK_SHADER_STAGE_COMPUTE_BIT;
    const VkShaderStageFlags graphics_only =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    for (int i = 1; i < 6; ++i) {
        bindings[i].binding = static_cast<u32>(i);
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = (i == 3 || i == 4 || i == 5) ? compute_only : graphics_only;
    }
    // The chunk table is read by both stages: the vertex shader needs origins and
    // the compute shader needs bounds.
    bindings[2].stageFlags = all;
    // The frame block is read by both shaders too.
    bindings[0].stageFlags = all;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 6;
    layout_info.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &set_layout_) != VK_SUCCESS) {
        error = "vkCreateDescriptorSetLayout failed";
        return false;
    }

    VkDescriptorPoolSize sizes[2]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = 1;
    sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sizes[1].descriptorCount = 5;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        error = "vkCreateDescriptorPool failed";
        return false;
    }

    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = descriptor_pool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &set_layout_;
    if (vkAllocateDescriptorSets(device_, &allocate, &descriptor_set_) != VK_SUCCESS) {
        error = "vkAllocateDescriptorSets failed";
        return false;
    }

    VkPipelineLayoutCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_info.setLayoutCount = 1;
    pipeline_info.pSetLayouts = &set_layout_;
    if (vkCreatePipelineLayout(device_, &pipeline_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        error = "vkCreatePipelineLayout failed";
        return false;
    }
    return true;
}

bool ChunkRenderer::create_pipelines(const std::string& shader_directory, VkFormat color_format,
                                     VkFormat depth_format, std::string& error) {
    const std::string base = shader_directory;
    const std::vector<char> vert_code = read_binary(base + "/chunk.vert.spv");
    const std::vector<char> frag_code = read_binary(base + "/chunk.frag.spv");
    const std::vector<char> comp_code = read_binary(base + "/cull.comp.spv");

    VkShaderModule vert = create_module(device_, vert_code, "chunk.vert", error);
    if (vert == VK_NULL_HANDLE) {
        return false;
    }
    VkShaderModule frag = create_module(device_, frag_code, "chunk.frag", error);
    if (frag == VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, vert, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    // No vertex attributes: the shader pulls packed voxels out of a storage
    // buffer, which is the whole point of the design.

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;

    const VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &color_format;
    rendering.depthAttachmentFormat = depth_format;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = pipeline_layout_;

    const VkResult graphics_result =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &graphics_pipeline_);

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);

    if (graphics_result != VK_SUCCESS) {
        error = "vkCreateGraphicsPipelines failed";
        return false;
    }

    VkShaderModule compute = create_module(device_, comp_code, "cull.comp", error);
    if (compute == VK_NULL_HANDLE) {
        return false;
    }

    VkPipelineShaderStageCreateInfo compute_stage{};
    compute_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compute_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compute_stage.module = compute;
    compute_stage.pName = "main";

    VkComputePipelineCreateInfo compute_info{};
    compute_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute_info.stage = compute_stage;
    compute_info.layout = pipeline_layout_;

    const VkResult compute_result =
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &compute_info, nullptr,
                                 &compute_pipeline_);
    vkDestroyShaderModule(device_, compute, nullptr);
    if (compute_result != VK_SUCCESS) {
        error = "vkCreateComputePipelines failed";
        return false;
    }
    return true;
}

bool ChunkRenderer::initialize(VkPhysicalDevice physical, VkDevice device, VkCommandPool pool,
                               VkQueue queue, VkFormat color_format, VkFormat depth_format,
                               const std::string& shader_directory, std::string& error) {
    physical_ = physical;
    device_ = device;
    command_pool_ = pool;
    queue_ = queue;

    // The frame block is the only thing written per frame, so it lives in host
    // visible memory and is rewritten in place.
    if (!create_buffer(physical_, device_, sizeof(FrameBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       frame_buffer_)) {
        error = "cannot allocate the frame uniform buffer";
        return false;
    }

    if (!create_layout(error)) {
        return false;
    }
    // The count buffer must exist before pipelines bind descriptors, even at zero.
    if (!create_buffer(physical_, device_, sizeof(u32),
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                           VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, count_buffer_)) {
        error = "cannot allocate the draw count buffer";
        return false;
    }

    return create_pipelines(shader_directory, color_format, depth_format, error);
}

void ChunkRenderer::destroy() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    destroy_buffer(device_, vertex_buffer_);
    destroy_buffer(device_, index_buffer_);
    destroy_buffer(device_, chunk_buffer_);
    destroy_buffer(device_, range_buffer_);
    destroy_buffer(device_, draw_buffer_);
    destroy_buffer(device_, count_buffer_);
    destroy_buffer(device_, frame_buffer_);

    if (graphics_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, graphics_pipeline_, nullptr);
    }
    if (compute_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, compute_pipeline_, nullptr);
    }
    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    }
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    }
    if (set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
}

GpuMeshHandle ChunkRenderer::add(const MeshData& mesh, f32 origin_x, f32 origin_y, f32 origin_z) {
    const u32 id = ++next_mesh_id_;
    if (mesh.empty()) {
        return GpuMeshHandle{id};
    }

    const MeshRange range = arena_.append(mesh);

    ChunkRecord record;
    record.id = id;
    record.origin[0] = origin_x;
    record.origin[1] = origin_y;
    record.origin[2] = origin_z;
    record.first_index = range.first_index;
    record.index_count = range.index_count;
    record.first_vertex = range.first_vertex;
    record.live = true;
    chunks_.push_back(record);

    arena_dirty_ = true;
    return GpuMeshHandle{id};
}

void ChunkRenderer::remove(GpuMeshHandle handle) {
    for (ChunkRecord& record : chunks_) {
        if (record.id == handle.id) {
            record.live = false;
            record.index_count = 0;
            arena_dirty_ = true;
            return;
        }
    }
}

bool ChunkRenderer::upload_arena() {
    const std::size_t vertex_bytes = arena_.vertices().size() * sizeof(PackedVertex);
    const std::size_t index_bytes = arena_.indices().size() * sizeof(u32);
    if (vertex_bytes == 0 || index_bytes == 0) {
        return false;
    }

    // A first cut: realiasing the whole arena when it changes is correct and
    // simple. Incremental suballocation is a later optimisation with a measurement
    // to justify it.
    if (!upload_buffer(physical_, device_, command_pool_, queue_, vertex_buffer_,
                       arena_.vertices().data(), vertex_bytes,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return false;
    }
    if (!upload_buffer(physical_, device_, command_pool_, queue_, index_buffer_,
                       arena_.indices().data(), index_bytes,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return false;
    }

    std::vector<ChunkGpu> chunk_data;
    std::vector<RangeGpu> range_data;
    chunk_data.reserve(chunks_.size());
    range_data.reserve(chunks_.size());
    for (const ChunkRecord& record : chunks_) {
        ChunkGpu chunk;
        chunk.origin_radius[0] = record.origin[0];
        chunk.origin_radius[1] = record.origin[1];
        chunk.origin_radius[2] = record.origin[2];
        chunk.origin_radius[3] = kChunkRadius;
        chunk_data.push_back(chunk);

        RangeGpu range{};
        range.first_index = record.first_index;
        range.index_count = record.live ? record.index_count : 0;
        range.first_vertex = record.first_vertex;
        range_data.push_back(range);
    }

    if (chunk_data.empty()) {
        capacity_ = 0;
        stats_ = DrawStatistics{};
        return true;
    }

    const VkDeviceSize chunk_bytes = chunk_data.size() * sizeof(ChunkGpu);
    const VkDeviceSize range_bytes = range_data.size() * sizeof(RangeGpu);
    const VkDeviceSize draw_bytes = static_cast<VkDeviceSize>(chunk_data.size()) * 20;

    if (!upload_buffer(physical_, device_, command_pool_, queue_, chunk_buffer_, chunk_data.data(),
                       chunk_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return false;
    }
    if (!upload_buffer(physical_, device_, command_pool_, queue_, range_buffer_, range_data.data(),
                       range_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return false;
    }

    destroy_buffer(device_, draw_buffer_);
    if (!create_buffer(physical_, device_, draw_bytes,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, draw_buffer_)) {
        return false;
    }

    capacity_ = static_cast<u32>(chunk_data.size());

    VkDescriptorBufferInfo infos[6]{};
    infos[0].buffer = frame_buffer_.buffer;
    infos[0].range = sizeof(FrameBlock);
    infos[1].buffer = vertex_buffer_.buffer;
    infos[1].range = vertex_bytes;
    infos[2].buffer = chunk_buffer_.buffer;
    infos[2].range = chunk_bytes;
    infos[3].buffer = draw_buffer_.buffer;
    infos[3].range = draw_bytes;
    infos[4].buffer = count_buffer_.buffer;
    infos[4].range = sizeof(u32);
    infos[5].buffer = range_buffer_.buffer;
    infos[5].range = range_bytes;

    VkWriteDescriptorSet writes[6]{};
    for (int i = 0; i < 6; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptor_set_;
        writes[i].dstBinding = static_cast<u32>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType =
            i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device_, 6, writes, 0, nullptr);

    stats_.live_meshes = capacity_;
    stats_.quad_count = static_cast<u32>(arena_.vertices().size() / 4);
    stats_.draw_calls = capacity_ > 0 ? 1u : 0u;
    return true;
}

void ChunkRenderer::write_frame_block(const FrameConstants& constants) {
    if (frame_buffer_.mapped == nullptr) {
        return;
    }
    FrameBlock block{};
    std::memcpy(block.view_projection, constants.view_projection, sizeof(block.view_projection));

    // The camera sits at the inverse of the view matrix's translation, but the
    // caller only supplies the combined matrix, so derive the eye from it.
    block.camera_position[0] = 0.0f;
    block.camera_position[1] = 0.0f;
    block.camera_position[2] = 0.0f;
    block.camera_position[3] = 1.0f;

    Mat4 view_projection;
    std::memcpy(view_projection.m, constants.view_projection, sizeof(view_projection.m));
    extract_frustum_planes(view_projection, block.frustum_planes);

    block.chunk_count = capacity_;
    block.max_chunks = capacity_;
    block.max_distance = kMaxDistance;
    std::memcpy(frame_buffer_.mapped, &block, sizeof(block));
}

void ChunkRenderer::prepare(VkCommandBuffer cmd, const FrameConstants& constants,
                            VkExtent2D extent) {
    (void)extent;
    if (arena_dirty_) {
        if (upload_arena()) {
            arena_dirty_ = false;
        }
    }
    if (capacity_ == 0) {
        return;
    }

    write_frame_block(constants);

    vkCmdFillBuffer(cmd, count_buffer_.buffer, 0, sizeof(u32), 0);

    VkMemoryBarrier to_compute{};
    to_compute.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    to_compute.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_compute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &to_compute, 0, nullptr, 0,
                         nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, 1,
                            &descriptor_set_, 0, nullptr);
    const u32 groups = (capacity_ + 63u) / 64u;
    vkCmdDispatch(cmd, groups, 1, 1);

    VkMemoryBarrier to_draw{};
    to_draw.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    to_draw.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_draw.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 1, &to_draw, 0, nullptr, 0, nullptr);
}

void ChunkRenderer::draw(VkCommandBuffer cmd, VkExtent2D extent) {
    if (capacity_ == 0) {
        return;
    }

    VkViewport viewport{};
    viewport.width = static_cast<f32>(extent.width);
    viewport.height = static_cast<f32>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = extent;

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
                            &descriptor_set_, 0, nullptr);
    vkCmdDrawIndexedIndirectCount(cmd, draw_buffer_.buffer, 0, count_buffer_.buffer, 0, capacity_,
                                  sizeof(u32) * 5);
}

}  // namespace sc::vk
