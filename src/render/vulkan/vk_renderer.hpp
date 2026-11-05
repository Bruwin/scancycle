#pragma once
// Vulkan 1.3 backend for the render hardware interface.
//
// Deliberate choices, recorded here because they are easy to undo by accident:
//
//  * The device is created from a Platform-provided surface, so this module
//    never includes a windowing library.
//  * Dynamic rendering (1.3 core) replaces render passes: there is one colour
//    target and one depth target, and framebuffer objects buy us nothing.
//  * The vertex stage pulls 8-byte packed voxels out of one storage buffer, so
//    the world is a single indirect draw rather than one draw per chunk.
#include "core/types.hpp"
#include "platform/platform.hpp"
#include "render/rhi.hpp"

#include <memory>
#include <string>

namespace sc::vk {

struct VulkanConfig {
    // SPIR-V is committed to the repository, so building needs no shader
    // compiler; this is where the blobs are read from at run time.
    std::string shader_directory = "src/render/vulkan/shaders/compiled";
    bool enable_validation = true;
};

// Returns nullptr when the machine cannot provide a suitable device. The reason
// is written to stderr so a user on unsupported hardware sees something useful
// instead of a blank window.
std::unique_ptr<RhiDevice> create_vulkan_rhi_device(Platform& platform,
                                                    const VulkanConfig& config);

}  // namespace sc::vk
