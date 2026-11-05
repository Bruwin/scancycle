// The windowed viewer: one SDL3 window, one Vulkan device, one frame loop.
// It exists so the renderer can be exercised on real hardware, which is the one
// thing the GPU-free test suite cannot do.
#include "core/image.hpp"
#include "render/camera.hpp"
#include "platform/platform.hpp"
#include "render/rhi.hpp"
#include "render/vulkan/vk_renderer.hpp"
#include "render/voxel_renderer.hpp"
#include "world/world.hpp"
#include "world/worldgen.hpp"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    sc::WindowDesc desc;
    sc::vk::VulkanConfig config;

    // Bounded runs let CI and a quick hardware check finish on their own.
    sc::i64 frame_limit = 0;
    std::string capture_path;
    std::string compare_path;
    sc::u32 tolerance = 6;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frame_limit = std::atoll(argv[++i]);
        } else if (std::strcmp(argv[i], "--shaders") == 0 && i + 1 < argc) {
            config.shader_directory = argv[++i];
        } else if (std::strcmp(argv[i], "--no-validation") == 0) {
            config.enable_validation = false;
        } else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) {
            capture_path = argv[++i];
        } else if (std::strcmp(argv[i], "--compare-with") == 0 && i + 1 < argc) {
            compare_path = argv[++i];
        } else if (std::strcmp(argv[i], "--capture-frame") == 0 && i + 1 < argc) {
            config.capture_frame = static_cast<sc::u32>(std::atoll(argv[++i]));
        } else if (std::strcmp(argv[i], "--tolerance") == 0 && i + 1 < argc) {
            tolerance = static_cast<sc::u32>(std::atoll(argv[++i]));
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf(
                "scancycle_viewer - Scan Cycle renderer\n"
                "\n"
                "usage: scancycle_viewer [--shaders DIR] [--no-validation] [--frames N]\n"
                "                         [--capture FILE] [--compare-with FILE]\n"
                "                         [--capture-frame N] [--tolerance N]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    config.capture_path = capture_path;

    std::unique_ptr<sc::Platform> platform = sc::create_sdl_platform();
    if (!platform->create_window(desc)) {
        std::fprintf(stderr, "cannot create a window\n");
        return 1;
    }

    std::unique_ptr<sc::RhiDevice> device = sc::vk::create_vulkan_rhi_device(*platform, config);
    if (!device) {
        std::fprintf(stderr,
                     "no Vulkan 1.3 device is available; this build needs a GPU with dynamic "
                     "rendering support\n");
        return 1;
    }

    std::printf("backend : %s\n", device->backend_name());
    std::printf("window  : %dx%d\n", desc.width, desc.height);

    // The generated plant, so every chunk the renderer draws came through
    // World::mesh with its neighbours folded in, instead of from a hand-built
    // stand-in that could disagree with the mesher.
    sc::World world;
    sc::WorldGenParams params;
    params.seed = 1;
    params.chunks_x = 4;
    params.chunks_z = 4;
    params.pillar_count = 40;
    sc::generate_plant(world, params);
    std::printf("chunks  : %zu in the world\n", world.chunk_count());

    sc::VoxelRenderer renderer(*device);

    sc::InputState input;
    sc::i64 frames = 0;

    while (!input.quit_requested && (frame_limit <= 0 || frames < frame_limit)) {
        platform->poll_events(input);

        sc::FrameConstants constants;
        // A captured frame has to be reproducible, or the reference it is checked
        // against would drift with how fast the machine happened to run.
        constants.time_seconds =
            capture_path.empty() ? static_cast<sc::f32>(platform->elapsed_seconds())
                                 : static_cast<sc::f32>(frames) / 60.0f;
        constants.viewport_width = static_cast<sc::u32>(desc.width);
        constants.viewport_height = static_cast<sc::u32>(desc.height);

        // A slow orbit around the plant, low enough that the pillars stand out
        // against the floor; a static screenshot is still obvious proof of a redraw.
        const sc::f32 angle = constants.time_seconds * 0.3f;
        const sc::Vec3 eye{55.0f * std::cos(angle), 24.0f, 55.0f * std::sin(angle)};
        const sc::Mat4 view =
            sc::look_at(eye, sc::Vec3{0.0f, 0.0f, 0.0f}, sc::Vec3{0.0f, 1.0f, 0.0f});
        const sc::Mat4 projection = sc::perspective(0.9f, 16.0f / 9.0f, 0.1f, 500.0f);
        const sc::Mat4 view_projection = sc::multiply(projection, view);
        std::memcpy(constants.view_projection, view_projection.m,
                    sizeof(constants.view_projection));

        // Residency follows the camera. The radius is in chunks, so a factory
        // far larger than the ring still costs a bounded amount per frame.
        const sc::ChunkCoord center =
            sc::chunk_of(static_cast<sc::i32>(eye.x), 0, static_cast<sc::i32>(eye.z));
        renderer.stream(world, center, 4);
        renderer.upload_pending(world, 4);

        device->begin_frame(constants);
        device->draw_frame();
        device->end_frame();

        const sc::DrawStatistics stats = device->statistics();
        if (++frames % 120 == 0) {
            std::printf("frame %lld  meshes %u  faces %u  draws %u\n",
                        static_cast<long long>(frames), stats.live_meshes, stats.quad_count,
                        stats.draw_calls);
        }
    }

    const sc::DrawStatistics final_stats = device->statistics();
    const sc::u32 errors = device->validation_errors();
    std::printf("summary: frames %lld, meshes %u, faces %u, draws %u, validation errors %u\n",
                static_cast<long long>(frames), final_stats.live_meshes, final_stats.quad_count,
                final_stats.draw_calls, errors);

    bool image_ok = true;
    if (!capture_path.empty()) {
        std::vector<sc::u8> pixels;
        sc::u32 pixel_width = 0;
        sc::u32 pixel_height = 0;
        if (!device->read_back(pixels, pixel_width, pixel_height)) {
            std::fprintf(stderr, "capture: the backend returned no frame\n");
            image_ok = false;
        } else {
            const sc::Thumbnail thumb =
                sc::make_thumbnail(pixels, pixel_width, pixel_height, 64, 36, false);
            if (thumb.empty() || !sc::write_ppm(capture_path, thumb)) {
                std::fprintf(stderr, "capture: cannot write '%s'\n", capture_path.c_str());
                image_ok = false;
            } else {
                std::printf("capture : %ux%u from %ux%u -> %s\n", thumb.width, thumb.height,
                            pixel_width, pixel_height, capture_path.c_str());
            }

            if (!compare_path.empty()) {
                sc::Thumbnail expected;
                if (!sc::read_ppm(compare_path, expected)) {
                    // First run: no reference committed yet, so record rather than fail.
                    std::printf("compare : no reference at %s, skipping\n", compare_path.c_str());
                } else {
                    const sc::ImageDifference diff = sc::compare_thumbnails(thumb, expected);
                    std::printf("compare : max %u, mean %.2f, changed %u pixels (tolerance %u)\n",
                                diff.max_channel_delta, diff.mean_channel_delta,
                                diff.changed_pixels, tolerance);
                    if (diff.within(tolerance)) {
                        std::printf("compare : OK\n");
                    } else {
                        std::printf("compare : REGRESSION\n");
                        image_ok = false;
                    }
                }
            }
        }
    }

    // Order matters: the swapchain holds the surface the platform owns, so the
    // renderer has to be destroyed before the window is.
    device.reset();
    platform->shutdown();
    return (errors == 0 && image_ok) ? 0 : 1;
}
