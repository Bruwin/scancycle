#include "platform/platform.hpp"

#include <chrono>

namespace sc {
namespace {

class NullPlatform final : public Platform {
public:
    bool create_window(const WindowDesc& desc) override {
        window_ = desc;
        created_ = true;
        return true;
    }

    void poll_events(InputState& input) override {
        // Headless backend: no OS events exist, so nothing can ever request a
        // quit unless a test or script sets it explicitly.
        input.quit_requested = false;
    }

    f64 elapsed_seconds() const override {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<f64>(now - started_).count();
    }

    const char* backend_name() const override { return "null"; }

    void shutdown() override { created_ = false; }

private:
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    WindowDesc window_;
    bool created_ = false;
};

}  // namespace

std::unique_ptr<Platform> create_null_platform() { return std::make_unique<NullPlatform>(); }

}  // namespace sc
