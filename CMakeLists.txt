cmake_minimum_required(VERSION 3.25)
project(ScanCycle VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(SC_WERROR "Treat compiler warnings as errors" ON)
option(SC_BUILD_TESTS "Build the test suite" ON)
# Off by default: the headless half must build on any machine, and CI has no GPU.
option(SC_ENABLE_VULKAN "Build the Vulkan 1.3 renderer and the windowed viewer" OFF)

add_library(sc_warnings INTERFACE)
if(MSVC)
    target_compile_options(sc_warnings INTERFACE /W4 /permissive-)
    if(SC_WERROR)
        target_compile_options(sc_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(sc_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
    if(SC_WERROR)
        target_compile_options(sc_warnings INTERFACE -Werror)
    endif()
endif()

# Layers, bottom to top. A target may only link targets declared above it.
#   L0 core                      types, logging, rng, jobs
#   L1 platform render world sim plc
#   L2 io assets ui lesson
#   L3 net app
#   L4 apps

add_library(sc_core
    src/core/image.cpp
    src/core/log.cpp
    src/core/job_system.cpp
)
target_include_directories(sc_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sc_core PRIVATE sc_warnings)

add_library(sc_platform src/platform/platform_null.cpp)
target_link_libraries(sc_platform PUBLIC sc_core PRIVATE sc_warnings)

add_library(sc_world
    src/world/voxel_chunk.cpp
    src/world/greedy_mesh.cpp
    src/world/world.cpp
    src/world/worldgen.cpp
)
target_link_libraries(sc_world PUBLIC sc_core PRIVATE sc_warnings)

add_library(sc_render
    src/render/rhi_null.cpp
    src/render/mesh_arena.cpp
    src/render/voxel_renderer.cpp
)
target_link_libraries(sc_render PUBLIC sc_core sc_world PRIVATE sc_warnings)

add_library(sc_sim src/sim/plant.cpp)
target_link_libraries(sc_sim PUBLIC sc_core PRIVATE sc_warnings)

add_library(sc_plc
    src/plc/value.cpp
    src/plc/lexer.cpp
    src/plc/parser.cpp
    src/plc/program.cpp
    src/plc/compiler.cpp
    src/plc/features.cpp
    src/plc/function_blocks.cpp
    src/plc/vm.cpp
    src/plc/declaration.cpp
    src/plc/source_text.cpp
    src/plc/netlist.cpp
    src/plc/il.cpp
    src/plc/ladder_text.cpp
)
target_link_libraries(sc_plc PUBLIC sc_core PRIVATE sc_warnings)

add_library(sc_io
    src/io/tag_database.cpp
    src/io/force_table.cpp
    src/io/sim_driver.cpp
    src/io/modbus.cpp
    src/io/commissioning.cpp
    src/io/socket_io.cpp
    src/io/modbus_tcp.cpp
    src/io/modbus_slave.cpp
)
target_link_libraries(sc_io PUBLIC sc_core sc_sim sc_plc PRIVATE sc_warnings)
if(WIN32)
    target_link_libraries(sc_io PRIVATE ws2_32)
endif()

add_library(sc_assets
    src/assets/material_library.cpp
    src/assets/kit_generator.cpp
)
target_link_libraries(sc_assets PUBLIC sc_core sc_world PRIVATE sc_warnings)

add_library(sc_ui src/ui/editor_model.cpp)
target_link_libraries(sc_ui PUBLIC sc_core sc_plc PRIVATE sc_warnings)

add_library(sc_lesson
    src/lesson/lesson.cpp
    src/lesson/grader.cpp
    src/lesson/grade_json.cpp
)
target_link_libraries(sc_lesson PUBLIC sc_core sc_sim PRIVATE sc_warnings)

add_library(sc_net
    src/net/portal_server.cpp
    src/net/http.cpp
    src/net/http_server.cpp
    src/net/websocket.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(sc_net PUBLIC sc_core sc_lesson PRIVATE sc_warnings Threads::Threads)
if(WIN32)
    target_link_libraries(sc_net PRIVATE ws2_32)
endif()

add_library(sc_app
    src/app/grade_log.cpp
    src/app/grade_events.cpp
    src/app/grades_page.cpp
    src/app/grades_pager.cpp
    src/app/grades_csv.cpp
    src/app/grades_coverage.cpp
    src/app/grades_students.cpp
    src/app/term_logs.cpp
    src/app/application.cpp
    src/app/ide_session.cpp
    src/app/ide_page.cpp
    src/app/watch_text.cpp
    src/app/lesson_runner.cpp
    src/app/lesson_api.cpp
)
target_link_libraries(sc_app PUBLIC sc_core sc_sim sc_plc sc_io sc_lesson sc_net sc_ui PRIVATE sc_warnings)

add_executable(scancycle_cli apps/scancycle_cli/main.cpp)
target_link_libraries(scancycle_cli PRIVATE sc_plc sc_world sc_app sc_lesson sc_warnings)

add_executable(scancycle_server apps/scancycle_server/main.cpp)
target_link_libraries(scancycle_server PRIVATE sc_net sc_lesson sc_app sc_warnings)

add_executable(scancycle_ide apps/scancycle_ide/main.cpp)
target_link_libraries(scancycle_ide PRIVATE sc_app sc_net sc_warnings)

if(SC_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    find_package(SDL3 CONFIG QUIET)
    if(NOT SDL3_FOUND)
        include(FetchContent)
        FetchContent_Declare(SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-3.4.20
            GIT_SHALLOW TRUE)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL3)
    endif()

    add_library(sc_platform_sdl src/platform/sdl_platform.cpp)
    target_link_libraries(sc_platform_sdl PUBLIC sc_platform SDL3::SDL3 PRIVATE sc_warnings)

    add_library(sc_render_vk
        src/render/vulkan/vk_renderer.cpp
        src/render/vulkan/vk_chunks.cpp
    )
    target_link_libraries(sc_render_vk PUBLIC sc_render sc_platform_sdl Vulkan::Vulkan
                                          PRIVATE sc_warnings)

    add_executable(scancycle_viewer apps/scancycle_viewer/main.cpp)
    target_link_libraries(scancycle_viewer PRIVATE sc_render_vk sc_world sc_warnings)
endif()

if(SC_BUILD_TESTS)
    enable_testing()
    add_executable(sc_tests
        tests/main.cpp
        tests/test_core.cpp
        tests/test_world.cpp
        tests/test_render.cpp
        tests/test_plc.cpp
        tests/test_netlist.cpp
        tests/test_il.cpp
        tests/test_ladder_text.cpp
        tests/test_sim.cpp
        tests/test_io.cpp
        tests/test_tools.cpp
        tests/test_ide.cpp
        tests/test_lesson.cpp
        tests/test_websocket.cpp
        tests/test_modbus_tcp.cpp
        tests/test_app.cpp
    )
    target_link_libraries(sc_tests PRIVATE
        sc_plc sc_world sc_render sc_sim sc_io sc_assets sc_ui sc_lesson sc_net sc_app
        sc_warnings Threads::Threads)
    add_test(NAME sc_tests COMMAND sc_tests)
    # The lesson gate: every reference program must pass its own rubric.
    add_test(NAME sc_lessons COMMAND scancycle_cli lesson)

    # The rules from AGENTS.md, through the same script the CI rules job runs, so
    # a local ctest and the pipeline cannot disagree. It needs a POSIX shell, so
    # Windows gets it from CI rather than from a Git Bash that may not be there.
    if(UNIX)
        add_test(NAME sc_rules
            COMMAND ${PROJECT_SOURCE_DIR}/tools/check_rules.sh
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    endif()
endif()
