// SDL3 window and Vulkan surface backend. Vulkan headers must come before
// SDL_vulkan.h: SDL only forward-declares the handles when vulkan.h has not
// already been included, and the two declarations do not agree.
#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "platform/platform.hpp"

#include <cstring>
#include <vector>

namespace sc {
namespace {

constexpr u32 kMaxKeyScancodes = 256;

class SdlPlatform final : public Platform {
public:
    ~SdlPlatform() override { shutdown(); }

    bool create_window(const WindowDesc& desc) override {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            error_ = SDL_GetError();
            return false;
        }
        initialized_ = true;

        window_ = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height,
                                   SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (window_ == nullptr) {
            error_ = SDL_GetError();
            return false;
        }

        start_counter_ = SDL_GetPerformanceCounter();
        frequency_ = SDL_GetPerformanceFrequency();
        return true;
    }

    void poll_events(InputState& input) override {
        input.quit_requested = false;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    input.quit_requested = true;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) {
                        input.quit_requested = true;
                    }
                    break;
                default:
                    break;
            }
        }

        // SDL reports keyboard state per scancode; the engine only needs a flat
        // array, so copy what fits rather than exposing SDL types upward.
        int count = 0;
        const bool* state = SDL_GetKeyboardState(&count);
        if (state != nullptr) {
            const int copy = count < static_cast<int>(kMaxKeyScancodes) ? count
                                                                       : static_cast<int>(kMaxKeyScancodes);
            for (int i = 0; i < copy; ++i) {
                input.keys[i] = state[i];
            }
        }

        float x = 0.0f;
        float y = 0.0f;
        SDL_GetMouseState(&x, &y);
        input.mouse_x = x;
        input.mouse_y = y;
    }

    f64 elapsed_seconds() const override {
        if (frequency_ == 0) {
            return 0.0;
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        return static_cast<f64>(now - start_counter_) / static_cast<f64>(frequency_);
    }

    const char* backend_name() const override { return "sdl3"; }

    void shutdown() override {
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (initialized_) {
            SDL_Quit();
            initialized_ = false;
        }
    }

    std::vector<const char*> vulkan_instance_extensions() const override {
        std::vector<const char*> extensions;
        Uint32 count = 0;
        char const* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
        if (names == nullptr) {
            return extensions;
        }
        extensions.reserve(count);
        for (Uint32 i = 0; i < count; ++i) {
            extensions.push_back(names[i]);
        }
        return extensions;
    }

    bool create_vulkan_surface(void* instance, void*& surface) const override {
        if (window_ == nullptr || instance == nullptr) {
            return false;
        }
        VkSurfaceKHR created = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window_, reinterpret_cast<VkInstance>(instance), nullptr,
                                      &created)) {
            return false;
        }
        surface = reinterpret_cast<void*>(created);
        return true;
    }

    void destroy_vulkan_surface(void* instance, void* surface) const override {
        if (instance == nullptr || surface == nullptr) {
            return;
        }
        SDL_Vulkan_DestroySurface(reinterpret_cast<VkInstance>(instance),
                                  reinterpret_cast<VkSurfaceKHR>(surface), nullptr);
    }

    void window_size_pixels(u32& width, u32& height) const override {
        int w = 0;
        int h = 0;
        if (window_ != nullptr && SDL_GetWindowSizeInPixels(window_, &w, &h)) {
            width = static_cast<u32>(w);
            height = static_cast<u32>(h);
            return;
        }
        width = 0;
        height = 0;
    }

    const std::string& error() const { return error_; }

private:
    SDL_Window* window_ = nullptr;
    bool initialized_ = false;
    Uint64 start_counter_ = 0;
    Uint64 frequency_ = 0;
    mutable std::string error_;
};

}  // namespace

std::unique_ptr<Platform> create_sdl_platform() { return std::make_unique<SdlPlatform>(); }

}  // namespace sc
