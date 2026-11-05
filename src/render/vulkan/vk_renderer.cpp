#include "render/vulkan/vk_renderer.hpp"

#include "render/vulkan/vk_chunks.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>
#include <limits>
#include <vector>

namespace sc::vk {
namespace {

constexpr u32 kFramesInFlight = 2;

// Counts only errors: warnings from lavapipe or a driver are noise, but a
// validation error means the frame is built wrong.
VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* user_data) {
    if (severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        return VK_FALSE;
    }
    std::fprintf(stderr, "[vulkan validation] %s\n", data->pMessage);
    if (user_data != nullptr) {
        ++(*static_cast<u32*>(user_data));
    }
    return VK_FALSE;
}

bool has_extension(const std::vector<VkExtensionProperties>& available, const char* name) {
    for (const VkExtensionProperties& entry : available) {
        if (std::strcmp(entry.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool has_layer(const std::vector<VkLayerProperties>& available, const char* name) {
    for (const VkLayerProperties& entry : available) {
        if (std::strcmp(entry.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

class VulkanRenderer final : public RhiDevice {
public:
    VulkanRenderer(Platform& platform, VulkanConfig config)
        : platform_(platform), config_(std::move(config)) {}

    ~VulkanRenderer() override { destroy(); }

    const std::string& last_error() const { return error_; }

    bool initialize();

    const char* backend_name() const override { return "vulkan"; }

    GpuMeshHandle upload_chunk_mesh(u32 chunk_x, u32 chunk_y, u32 chunk_z,
                                    const MeshData& mesh) override;
    void release_mesh(GpuMeshHandle handle) override;

    void begin_frame(const FrameConstants& constants) override;
    void draw_frame() override;
    void end_frame() override;

    DrawStatistics statistics() const override { return stats_; }
    u32 validation_errors() const override { return validation_errors_; }
    bool read_back(std::vector<u8>& pixels, u32& width, u32& height) override;

private:
    bool fail(const char* what, VkResult result);
    bool fail(const std::string& message);

    bool create_instance();
    bool create_surface();
    bool pick_physical_device();
    bool create_device();
    bool create_swapchain();
    bool create_depth_target();
    bool create_commands();
    bool create_sync_objects();
    bool recreate_swapchain();

    void destroy();
    bool create_capture_buffer();

    Platform& platform_;
    VulkanConfig config_;
    std::string error_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    u32 queue_family_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D extent_{0, 0};
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
    std::vector<VkSemaphore> image_ready_;

    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> command_buffers_{};
    std::array<VkSemaphore, kFramesInFlight> frame_ready_{};
    std::array<VkFence, kFramesInFlight> frame_fences_{};

    u32 frame_index_ = 0;
    u32 image_index_ = 0;
    bool frame_open_ = false;
    bool swapchain_dirty_ = false;

    std::unique_ptr<ChunkRenderer> chunks_;
    FrameConstants constants_{};
    // Capture is a diagnostic path: one synchronous frame, only when asked for.
    bool capture_supported_ = false;
    Buffer capture_buffer_;
    std::vector<u8> captured_;
    u32 captured_width_ = 0;
    u32 captured_height_ = 0;
    u32 frame_number_ = 0;

    DrawStatistics stats_{};
    u32 validation_errors_ = 0;
};

bool VulkanRenderer::fail(const char* what, VkResult result) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s failed with VkResult %d", what,
                  static_cast<int>(result));
    error_ = buffer;
    return false;
}

bool VulkanRenderer::fail(const std::string& message) {
    error_ = message;
    return false;
}

bool VulkanRenderer::create_instance() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Scan Cycle";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "ScanCycle";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_3;

    u32 extension_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> available(extension_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, available.data());

    std::vector<const char*> extensions = platform_.vulkan_instance_extensions();
    if (extensions.empty()) {
        return fail("the platform did not provide Vulkan instance extensions");
    }
    for (const char* required : {VK_KHR_SURFACE_EXTENSION_NAME}) {
        if (!has_extension(available, required)) {
            return fail(std::string("required Vulkan extension missing: ") + required);
        }
    }

    // MoltenVK exposes a portability surface; enabling enumeration is harmless
    // elsewhere and required on macOS.
    bool portability = false;
    if (has_extension(available, "VK_KHR_portability_enumeration")) {
        extensions.push_back("VK_KHR_portability_enumeration");
        portability = true;
    }

    std::vector<const char*> layers;
    if (config_.enable_validation) {
        u32 layer_count = 0;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        std::vector<VkLayerProperties> available_layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
        if (has_layer(available_layers, "VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        }
        // A machine without the SDK simply runs without validation rather than
        // refusing to start.
    }

    bool debug_utils = false;
    if (!layers.empty() && has_extension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        debug_utils = true;
    }

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<u32>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<u32>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    if (portability) {
        info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    const VkResult result = vkCreateInstance(&info, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        return fail("vkCreateInstance", result);
    }

    if (debug_utils) {
        auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (create_messenger != nullptr) {
            VkDebugUtilsMessengerCreateInfoEXT messenger_info{};
            messenger_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
            messenger_info.pfnUserCallback = debug_callback;
            messenger_info.pUserData = &validation_errors_;
            create_messenger(instance_, &messenger_info, nullptr, &debug_messenger_);
        }
    }
    return true;
}

bool VulkanRenderer::create_surface() {
    void* created = nullptr;
    if (!platform_.create_vulkan_surface(reinterpret_cast<void*>(instance_), created)) {
        return fail("the platform could not create a Vulkan surface");
    }
    surface_ = reinterpret_cast<VkSurfaceKHR>(created);
    return true;
}

bool VulkanRenderer::pick_physical_device() {
    u32 count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        return fail("no Vulkan-capable GPU was found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    u32 chosen_family = 0;

    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3) {
            continue;  // dynamic rendering and synchronization2 are the floor
        }

        u32 family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());

        for (u32 i = 0; i < family_count; ++i) {
            const VkQueueFlags flags = families[i].queueFlags;
            if ((flags & VK_QUEUE_GRAPHICS_BIT) == 0 || (flags & VK_QUEUE_COMPUTE_BIT) == 0) {
                continue;
            }
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present);
            if (present == VK_FALSE) {
                continue;
            }

            VkPhysicalDeviceVulkan13Features features13{};
            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            VkPhysicalDeviceVulkan12Features probe12{};
            probe12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            features13.pNext = &probe12;
            VkPhysicalDeviceFeatures2 features{};
            features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features.pNext = &features13;
            vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (features13.dynamicRendering != VK_TRUE || features13.synchronization2 != VK_TRUE) {
                continue;
            }
            // vkCmdDrawIndexedIndirectCount is where the whole chunk path ends up,
            // so a device without the feature cannot draw the world at all.
            if (probe12.drawIndirectCount != VK_TRUE || features.features.multiDrawIndirect != VK_TRUE ||
                features.features.drawIndirectFirstInstance != VK_TRUE) {
                continue;
            }

            chosen = candidate;
            chosen_family = i;
            break;
        }
        if (chosen != VK_NULL_HANDLE) {
            break;
        }
    }

    if (chosen == VK_NULL_HANDLE) {
        return fail("no GPU provides Vulkan 1.3 with dynamic rendering, synchronization2, "
                    "indirect draw count and a presentable graphics queue");
    }

    physical_ = chosen;
    queue_family_ = chosen_family;
    return true;
}

bool VulkanRenderer::create_device() {
    u32 extension_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> available(extension_count);
    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &extension_count, available.data());

    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (has_extension(available, "VK_KHR_portability_subset")) {
        extensions.push_back("VK_KHR_portability_subset");
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    // gl_InstanceIndex must include the base instance: the indirect draw carries
    // the chunk index in firstInstance.
    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features11.shaderDrawParameters = VK_TRUE;
    features11.pNext = &features13;

    // Core in 1.2 but still gated behind a feature, and this is the command the
    // entire chunk path collapses into.
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.drawIndirectCount = VK_TRUE;
    features12.pNext = &features11;

    VkPhysicalDeviceFeatures features{};
    features.drawIndirectFirstInstance = VK_TRUE;
    features.multiDrawIndirect = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.features = features;
    features2.pNext = &features12;

    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pNext = &features2;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue_info;
    info.enabledExtensionCount = static_cast<u32>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

    const VkResult result = vkCreateDevice(physical_, &info, nullptr, &device_);
    if (result != VK_SUCCESS) {
        return fail("vkCreateDevice", result);
    }
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    return true;
}

bool VulkanRenderer::create_swapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);
    if (result != VK_SUCCESS) {
        return fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
    }

    u32 format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, formats.data());
    if (formats.empty()) {
        return fail("the surface offers no formats");
    }

    VkSurfaceFormatKHR format = formats.front();
    for (const VkSurfaceFormatKHR& candidate : formats) {
        if (candidate.format == VK_FORMAT_B8G8R8A8_SRGB &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = candidate;
            break;
        }
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == std::numeric_limits<u32>::max()) {
        u32 width = 0;
        u32 height = 0;
        platform_.window_size_pixels(width, height);
        extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        return fail("the window has no drawable area");
    }

    u32 image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        image_count = std::min(image_count, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = image_count;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    // A capture copies out of this image, which needs the transfer usage;
    // a surface that cannot provide it simply reports no capture instead.
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    capture_supported_ = !config_.capture_path.empty() &&
                        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (capture_supported_) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    info.imageUsage = usage;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // always available, and vsync keeps a 2013 box cool
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    result = vkCreateSwapchainKHR(device_, &info, nullptr, &created);
    if (result != VK_SUCCESS) {
        return fail("vkCreateSwapchainKHR", result);
    }

    destroy_buffer(device_, capture_buffer_);
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }
    swapchain_ = created;
    swapchain_format_ = format.format;
    extent_ = extent;

    for (VkImageView view : image_views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    image_views_.clear();

    u32 actual = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &actual, nullptr);
    images_.resize(actual);
    vkGetSwapchainImagesKHR(device_, swapchain_, &actual, images_.data());

    image_views_.resize(images_.size());
    for (std::size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        result = vkCreateImageView(device_, &view_info, nullptr, &image_views_[i]);
        if (result != VK_SUCCESS) {
            return fail("vkCreateImageView(swapchain)", result);
        }
    }

    if (image_ready_.size() != images_.size()) {
        for (VkSemaphore semaphore : image_ready_) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
        image_ready_.resize(images_.size());
        for (std::size_t i = 0; i < image_ready_.size(); ++i) {
            VkSemaphoreCreateInfo semaphore_info{};
            semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            result = vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_ready_[i]);
            if (result != VK_SUCCESS) {
                return fail("vkCreateSemaphore(image ready)", result);
            }
        }
    }

    return true;
}

bool VulkanRenderer::create_depth_target() {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {extent_.width, extent_.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = VK_FORMAT_D32_SFLOAT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateImage(device_, &info, nullptr, &depth_image_);
    if (result != VK_SUCCESS) {
        return fail("vkCreateImage(depth)", result);
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, depth_image_, &requirements);

    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
    u32 type_index = std::numeric_limits<u32>::max();
    for (u32 i = 0; i < memory.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1u << i)) != 0 &&
            (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
            type_index = i;
            break;
        }
    }
    if (type_index == std::numeric_limits<u32>::max()) {
        return fail("no device-local memory type for the depth buffer");
    }

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type_index;
    result = vkAllocateMemory(device_, &allocate, nullptr, &depth_memory_);
    if (result != VK_SUCCESS) {
        return fail("vkAllocateMemory(depth)", result);
    }
    vkBindImageMemory(device_, depth_image_, depth_memory_, 0);

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = depth_image_;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_D32_SFLOAT;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    result = vkCreateImageView(device_, &view, nullptr, &depth_view_);
    if (result != VK_SUCCESS) {
        return fail("vkCreateImageView(depth)", result);
    }
    return true;
}

bool VulkanRenderer::create_commands() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_;
    VkResult result = vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_);
    if (result != VK_SUCCESS) {
        return fail("vkCreateCommandPool", result);
    }

    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = command_pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = kFramesInFlight;
    result = vkAllocateCommandBuffers(device_, &allocate, command_buffers_.data());
    if (result != VK_SUCCESS) {
        return fail("vkAllocateCommandBuffers", result);
    }
    return true;
}

bool VulkanRenderer::create_sync_objects() {
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (u32 i = 0; i < kFramesInFlight; ++i) {
        VkResult result = vkCreateSemaphore(device_, &semaphore_info, nullptr, &frame_ready_[i]);
        if (result != VK_SUCCESS) {
            return fail("vkCreateSemaphore(frame ready)", result);
        }
        result = vkCreateFence(device_, &fence_info, nullptr, &frame_fences_[i]);
        if (result != VK_SUCCESS) {
            return fail("vkCreateFence", result);
        }
    }
    return true;
}

bool VulkanRenderer::create_capture_buffer() {
    destroy_buffer(device_, capture_buffer_);
    if (!capture_supported_) {
        return true;
    }
    const VkDeviceSize size = static_cast<VkDeviceSize>(extent_.width) * extent_.height * 4;
    if (!create_buffer(physical_, device_, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       capture_buffer_)) {
        return fail("cannot allocate the capture buffer");
    }
    return true;
}

bool VulkanRenderer::initialize() {
    if (!create_instance()) {
        return false;
    }
    if (!create_surface()) {
        return false;
    }
    if (!pick_physical_device()) {
        return false;
    }
    if (!create_device()) {
        return false;
    }
    if (!create_swapchain()) {
        return false;
    }
    if (!create_depth_target()) {
        return false;
    }
    if (!create_capture_buffer()) {
        return false;
    }
    if (!create_commands()) {
        return false;
    }
    if (!create_sync_objects()) {
        return false;
    }

    chunks_ = std::make_unique<ChunkRenderer>();
    std::string chunk_error;
    if (!chunks_->initialize(physical_, device_, command_pool_, queue_, swapchain_format_,
                             VK_FORMAT_D32_SFLOAT, config_.shader_directory, chunk_error)) {
        return fail(chunk_error);
    }
    return true;
}

bool VulkanRenderer::recreate_swapchain() {
    vkDeviceWaitIdle(device_);
    vkDestroyImageView(device_, depth_view_, nullptr);
    depth_view_ = VK_NULL_HANDLE;
    vkDestroyImage(device_, depth_image_, nullptr);
    depth_image_ = VK_NULL_HANDLE;
    vkFreeMemory(device_, depth_memory_, nullptr);
    depth_memory_ = VK_NULL_HANDLE;

    if (!create_swapchain()) {
        return false;
    }
    if (!create_depth_target()) {
        return false;
    }
    return create_capture_buffer();
}

GpuMeshHandle VulkanRenderer::upload_chunk_mesh(u32 chunk_x, u32 chunk_y, u32 chunk_z,
                                                const MeshData& mesh) {
    if (!chunks_) {
        return GpuMeshHandle{};
    }
    constexpr f32 kChunkExtent = 32.0f;
    return chunks_->add(mesh, static_cast<f32>(chunk_x) * kChunkExtent,
                        static_cast<f32>(chunk_y) * kChunkExtent,
                        static_cast<f32>(chunk_z) * kChunkExtent);
}

void VulkanRenderer::release_mesh(GpuMeshHandle handle) {
    if (chunks_) {
        chunks_->remove(handle);
    }
}

void VulkanRenderer::begin_frame(const FrameConstants& constants) {
    constants_ = constants;

    if (vkWaitForFences(device_, 1, &frame_fences_[frame_index_], VK_TRUE, UINT64_MAX) !=
        VK_SUCCESS) {
        return;
    }

    const VkResult acquired =
        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame_ready_[frame_index_],
                              VK_NULL_HANDLE, &image_index_);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_dirty_ = true;
        return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        fail("vkAcquireNextImageKHR", acquired);
        return;
    }

    vkResetFences(device_, 1, &frame_fences_[frame_index_]);
    frame_open_ = true;
}

void VulkanRenderer::draw_frame() {
    if (!frame_open_) {
        return;
    }

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        return;
    }

    VkImageMemoryBarrier2 to_color{};
    to_color.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_color.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    to_color.srcAccessMask = 0;
    to_color.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    to_color.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    to_color.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.image = images_[image_index_];
    to_color.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_color.subresourceRange.levelCount = 1;
    to_color.subresourceRange.layerCount = 1;

    // The depth target is created in UNDEFINED and must be transitioned before a
    // dynamic-rendering pass can use it; skipping this is undefined behaviour that
    // most drivers happen to survive and some do not.
    VkImageMemoryBarrier2 to_depth{};
    to_depth.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_depth.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    to_depth.srcAccessMask = 0;
    to_depth.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    to_depth.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    to_depth.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_depth.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    to_depth.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_depth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_depth.image = depth_image_;
    to_depth.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    to_depth.subresourceRange.levelCount = 1;
    to_depth.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier2 to_targets[2] = {to_color, to_depth};

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 2;
    dependency.pImageMemoryBarriers = to_targets;
    vkCmdPipelineBarrier2(cmd, &dependency);

    if (chunks_) {
        chunks_->prepare(cmd, constants_, extent_);
    }

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = image_views_[image_index_];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.58f, 0.66f, 0.76f, 1.0f}};  // horizon blue

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = depth_view_;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo render{};
    render.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    render.renderArea = {{0, 0}, extent_};
    render.layerCount = 1;
    render.colorAttachmentCount = 1;
    render.pColorAttachments = &color;
    render.pDepthAttachment = &depth;

    vkCmdBeginRendering(cmd, &render);
    if (chunks_) {
        chunks_->draw(cmd, extent_);
    }
    vkCmdEndRendering(cmd);

    const bool capture_now =
        capture_buffer_.valid() && frame_number_ == config_.capture_frame;

    VkImageMemoryBarrier2 to_present = to_color;
    to_present.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    to_present.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    to_present.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    to_present.dstAccessMask = 0;
    to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    if (capture_now) {
        // Copy while the image is ours, then hand it to the presentation engine as
        // usual: the present barrier below starts from TRANSFER_SRC in that case.
        VkImageMemoryBarrier2 to_copy = to_present;
        to_copy.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_copy.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        to_copy.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        to_copy.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        to_copy.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;

        VkDependencyInfo copy_dependency{};
        copy_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        copy_dependency.imageMemoryBarrierCount = 1;
        copy_dependency.pImageMemoryBarriers = &to_copy;
        vkCmdPipelineBarrier2(cmd, &copy_dependency);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(cmd, images_[image_index_], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               capture_buffer_.buffer, 1, &region);

        to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_present.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        to_present.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    }

    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &to_present;
    vkCmdPipelineBarrier2(cmd, &dependency);

    vkEndCommandBuffer(cmd);

    const VkSemaphore wait_semaphore = frame_ready_[frame_index_];
    const VkSemaphore signal_semaphore = image_ready_[image_index_];

    VkSemaphoreSubmitInfo wait_info{};
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = wait_semaphore;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal_info{};
    signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = signal_semaphore;
    signal_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkCommandBufferSubmitInfo command_info{};
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_info.commandBuffer = cmd;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait_info;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_info;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal_info;

    const VkResult result = vkQueueSubmit2(queue_, 1, &submit, frame_fences_[frame_index_]);
    if (result != VK_SUCCESS) {
        fail("vkQueueSubmit2", result);
        frame_open_ = false;
        return;
    }

    if (capture_now && capture_buffer_.mapped != nullptr) {
        // One synchronous frame is acceptable for a diagnostic path and is the only
        // way to read the pixels before the presentation engine takes the image.
        vkWaitForFences(device_, 1, &frame_fences_[frame_index_], VK_TRUE, UINT64_MAX);
        const std::size_t bytes = static_cast<std::size_t>(extent_.width) * extent_.height * 4;
        const auto* base = static_cast<const u8*>(capture_buffer_.mapped);
        captured_.assign(base, base + bytes);
        captured_width_ = extent_.width;
        captured_height_ = extent_.height;
    }
    ++frame_number_;

    stats_ = chunks_ ? chunks_->statistics() : DrawStatistics{};
}

bool VulkanRenderer::read_back(std::vector<u8>& pixels, u32& width, u32& height) {
    if (captured_.empty()) {
        return false;
    }
    pixels = std::move(captured_);
    captured_.clear();
    width = captured_width_;
    height = captured_height_;

    // Callers get RGBA whatever the swapchain offered.
    if (swapchain_format_ == VK_FORMAT_B8G8R8A8_SRGB ||
        swapchain_format_ == VK_FORMAT_B8G8R8A8_UNORM) {
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i + 2]);
        }
    }
    return true;
}

void VulkanRenderer::end_frame() {
    if (!frame_open_) {
        return;
    }
    frame_open_ = false;

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &image_ready_[image_index_];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &image_index_;

    const VkResult result = vkQueuePresentKHR(queue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        swapchain_dirty_ = true;
    }

    frame_index_ = (frame_index_ + 1) % kFramesInFlight;

    if (swapchain_dirty_) {
        swapchain_dirty_ = false;
        recreate_swapchain();
    }
}

void VulkanRenderer::destroy() {
    if (device_ == VK_NULL_HANDLE && instance_ == VK_NULL_HANDLE) {
        return;  // already torn down
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    if (chunks_) {
        chunks_->destroy();
        chunks_.reset();
    }

    for (VkSemaphore semaphore : image_ready_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
    }
    image_ready_.clear();

    for (u32 i = 0; i < kFramesInFlight; ++i) {
        if (frame_ready_[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, frame_ready_[i], nullptr);
        }
        if (frame_fences_[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device_, frame_fences_[i], nullptr);
        }
    }

    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
    }
    if (depth_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depth_view_, nullptr);
    }
    if (depth_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, depth_image_, nullptr);
    }
    if (depth_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, depth_memory_, nullptr);
    }
    for (VkImageView view : image_views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    image_views_.clear();

    destroy_buffer(device_, capture_buffer_);
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        platform_.destroy_vulkan_surface(reinterpret_cast<void*>(instance_),
                                         reinterpret_cast<void*>(surface_));
    }
    if (debug_messenger_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        auto destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_messenger != nullptr) {
            destroy_messenger(instance_, debug_messenger_, nullptr);
        }
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
    // Leave nothing dangling: the viewer destroys the device explicitly and the
    // owning unique_ptr destroys it again on the way out.
    instance_ = VK_NULL_HANDLE;
    physical_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    surface_ = VK_NULL_HANDLE;
    swapchain_ = VK_NULL_HANDLE;
    debug_messenger_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
}

}  // namespace

std::unique_ptr<RhiDevice> create_vulkan_rhi_device(Platform& platform,
                                                    const VulkanConfig& config) {
    auto renderer = std::make_unique<VulkanRenderer>(platform, config);
    if (!renderer->initialize()) {
        std::fprintf(stderr, "[vulkan] %s\n", renderer->last_error().c_str());
        return nullptr;
    }
    return renderer;
}

}  // namespace sc::vk
