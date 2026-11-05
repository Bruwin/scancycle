#pragma once
// Window, input and presentation clock. Deliberately thin: the platform layer
// owns the OS surface and nothing else. Simulation code never sees this type.
#include "core/types.hpp"

#include <memory>
#include <string>

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
};

std::unique_ptr<Platform> create_null_platform();

}  // namespace sc
