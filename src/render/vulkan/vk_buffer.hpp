#pragma once
// Buffer plumbing shared by the Vulkan backend. Kept header-only and small: a
// general allocator is deliberate overkill until the renderer proves it needs
// one, and every allocation here is a long-lived whole-buffer upload.
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <limits>

namespace sc::vk {

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;

    bool valid() const { return buffer != VK_NULL_HANDLE; }
};

inline u32 find_memory_type(VkPhysicalDevice physical, u32 type_bits,
                            VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    for (u32 i = 0; i < memory.memoryTypeCount; ++i) {
        const bool supported = (type_bits & (1u << i)) != 0;
        const bool matches = (memory.memoryTypes[i].propertyFlags & properties) == properties;
        if (supported && matches) {
            return i;
        }
    }
    return std::numeric_limits<u32>::max();
}

inline bool create_buffer(VkPhysicalDevice physical, VkDevice device, VkDeviceSize size,
                          VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                          Buffer& out) {
    if (size == 0) {
        return false;
    }

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &info, nullptr, &out.buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, out.buffer, &requirements);

    const u32 type_index = find_memory_type(physical, requirements.memoryTypeBits, properties);
    if (type_index == std::numeric_limits<u32>::max()) {
        vkDestroyBuffer(device, out.buffer, nullptr);
        out.buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type_index;
    if (vkAllocateMemory(device, &allocate, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, out.buffer, nullptr);
        out.buffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device, out.buffer, out.memory, 0);
    out.size = size;

    if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
        vkMapMemory(device, out.memory, 0, size, 0, &out.mapped);
    }
    return true;
}

inline void destroy_buffer(VkDevice device, Buffer& buffer) {
    if (buffer.mapped != nullptr) {
        vkUnmapMemory(device, buffer.memory);
        buffer.mapped = nullptr;
    }
    if (buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
        buffer.buffer = VK_NULL_HANDLE;
    }
    if (buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer.memory, nullptr);
        buffer.memory = VK_NULL_HANDLE;
    }
    buffer.size = 0;
}

// One-shot copy from host memory into a device-local buffer. Used at load time
// and whenever the world arena grows, never per frame.
inline bool upload_buffer(VkPhysicalDevice physical, VkDevice device, VkCommandPool pool,
                          VkQueue queue, Buffer& target, const void* data, VkDeviceSize size,
                          VkBufferUsageFlags usage) {
    Buffer staging;
    if (!create_buffer(physical, device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       staging)) {
        return false;
    }
    if (staging.mapped != nullptr) {
        std::memcpy(staging.mapped, data, static_cast<std::size_t>(size));
    }

    destroy_buffer(device, target);
    if (!create_buffer(physical, device, size,
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, target)) {
        destroy_buffer(device, staging);
        return false;
    }

    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &allocate, &cmd) != VK_SUCCESS) {
        destroy_buffer(device, staging);
        return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkBufferCopy copy{};
    copy.size = size;
    vkCmdCopyBuffer(cmd, staging.buffer, target.buffer, 1, &copy);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &cmd);
    destroy_buffer(device, staging);
    return true;
}

}  // namespace sc::vk
