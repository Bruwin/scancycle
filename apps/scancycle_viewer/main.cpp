// The windowed viewer: one SDL3 window, one Vulkan device, one frame loop.
// It exists so the renderer can be exercised on real hardware, which is the one
// thing the GPU-free test suite cannot do.
#include "render/camera.hpp"
#include "platform/platform.hpp"
#include "render/rhi.hpp"
#include "render/vulkan/vk_renderer.hpp"
#include "world/mesh.hpp"
#include "world/voxel_chunk.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    sc::WindowDesc desc;
    sc::vk::VulkanConfig config;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            // Bounded runs let CI and a quick hardware check finish on their own.
            ++i;
        } else if (std::strcmp(argv[i], "--shaders") == 0 && i + 1 < argc) {
            config.shader_directory = argv[++i];
        } else if (std::strcmp(argv[i], "--no-validation") == 0) {
            config.enable_validation = false;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf(
                "scancycle_viewer - Scan Cycle renderer\n"
                "\n"
                "usage: scancycle_viewer [--shaders DIR] [--no-validation] [--frames N]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

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

    // A small stand-in world until world generation is wired to the viewer: a
    // floor with pillars, four by four chunks, so the first frames have geometry.
    sc::u32 chunks_uploaded = 0;
    for (sc::u32 cz = 0; cz < 4; ++cz) {
        for (sc::u32 cx = 0; cx < 4; ++cx) {
            sc::VoxelChunk chunk;
            for (int z = 0; z < sc::kChunkSize; ++z) {
                for (int x = 0; x < sc::kChunkSize; ++x) {
                    chunk.set(x, 0, z, 1);
                }
            }
            for (int y = 1; y < 12; ++y) {
                chunk.set(8, y, 8, 2);
                chunk.set(24, y, 24, 3);
            }
            device->upload_chunk_mesh(cx, 0, cz, sc::greedy_mesh(chunk));
            ++chunks_uploaded;
        }
    }
    std::printf("chunks  : %u\n", chunks_uploaded);

    sc::InputState input;
    sc::i64 frames = 0;

    while (!input.quit_requested) {
        platform->poll_events(input);

        sc::FrameConstants constants;
        constants.time_seconds = static_cast<sc::f32>(platform->elapsed_seconds());
        constants.viewport_width = static_cast<sc::u32>(desc.width);
        constants.viewport_height = static_cast<sc::u32>(desc.height);

        // A slow orbit around the test world, so a static screenshot is still
        // obvious proof that the frame is being redrawn.
        const sc::f32 angle = constants.time_seconds * 0.3f;
        const sc::Vec3 eye{64.0f + 70.0f * std::cos(angle), 46.0f,
                           64.0f + 70.0f * std::sin(angle)};
        const sc::Mat4 view =
            sc::look_at(eye, sc::Vec3{64.0f, 0.0f, 64.0f}, sc::Vec3{0.0f, 1.0f, 0.0f});
        const sc::Mat4 projection = sc::perspective(0.9f, 16.0f / 9.0f, 0.1f, 500.0f);
        const sc::Mat4 view_projection = sc::multiply(projection, view);
        std::memcpy(constants.view_projection, view_projection.m,
                    sizeof(constants.view_projection));

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

    platform->shutdown();
    return 0;
}
