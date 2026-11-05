#pragma once
// Window, input and presentation clock. Deliberately thin: the platform layer
// owns the OS surface and nothing else. Simulation code never sees this type.
#include "core/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sc {

struct WindowDesc {
    std::string title = "Scan Cycle";
    i32 width = 1600;
    i32 height = 900;
};

struct InputState {
    static constexpr u32 kKeyCount = 256;
    bool keys[kKeyCount] = {};
    f32 mouse_x = 0.0f;
    f32 mouse_y = 0.0f;
    bool quit_requested = false;
};

class Platform {
public:
    virtual ~Platform() = default;

    virtual bool create_window(const WindowDesc& desc) = 0;
    virtual void poll_events(InputState& input) = 0;
    // Presentation time only. Never fed into the simulation directly; it drives
    // the fixed-step accumulator, which is what the simulation sees.
    virtual f64 elapsed_seconds() const = 0;
    virtual const char* backend_name() const = 0;
    virtual void shutdown() = 0;

    // Graphics surface plumbing. The platform owns the window and therefore owns
    // surface creation, so the renderer only ever handles an opaque
    // VkSurfaceKHR and never learns which windowing library is in use. These
    // default to "no surface", which is what keeps the null platform working
    // with every GPU-free test.
    virtual std::vector<const char*> vulkan_instance_extensions() const { return {}; }
    virtual bool create_vulkan_surface(void* instance, void*& surface) const {
        (void)instance;
        (void)surface;
        return false;
    }
    virtual void destroy_vulkan_surface(void* instance, void* surface) const {
        (void)instance;
        (void)surface;
    }
    // Size of the drawable surface in pixels, which is not the window size on a
    // high-DPI display.
    virtual void window_size_pixels(u32& width, u32& height) const {
        width = 0;
        height = 0;
    }
};

std::unique_ptr<Platform> create_null_platform();

// SDL3 backend, built only when SC_ENABLE_VULKAN is on.
std::unique_ptr<Platform> create_sdl_platform();

}  // namespace sc
