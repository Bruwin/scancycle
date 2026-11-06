#include "app/shell_page.hpp"
#include "net/http_server.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

// The 1.0 shell. It is a local presentation surface, not a new simulation or a
// replacement for the IDE/portal servers. --teacher only reveals the portal
// entry; it is not authentication.
int main(int argc, char** argv) {
    unsigned port = 8070;
    long requests = 0;
    sc::app::ShellOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--teacher") {
            options.teacher_mode = true;
        } else if (arg == "--help") {
            std::printf(
                "scancycle_shell [--port=8070] [--teacher] "
                "[--ide-url=URL] [--portal-url=URL] [--requests=N]\n");
            return 0;
        } else if (arg.rfind("--port=", 0) == 0) {
            char* end = nullptr;
            const long value = std::strtol(arg.c_str() + 7, &end, 10);
            if (end == arg.c_str() + 7 || *end != '\0' || value <= 0 || value > 65535) {
                std::fprintf(stderr, "shell: '%s' is not a port\n", arg.c_str());
                return 2;
            }
            port = static_cast<unsigned>(value);
        } else if (arg.rfind("--requests=", 0) == 0) {
            requests = std::strtol(arg.c_str() + 11, nullptr, 10);
        } else if (arg.rfind("--ide-url=", 0) == 0) {
            options.ide_url = arg.substr(10);
            if (options.ide_url.empty()) {
                std::fprintf(stderr, "shell: --ide-url= needs a URL\n");
                return 2;
            }
        } else if (arg.rfind("--portal-url=", 0) == 0) {
            options.portal_url = arg.substr(13);
            if (options.portal_url.empty()) {
                std::fprintf(stderr, "shell: --portal-url= needs a URL\n");
                return 2;
            }
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    sc::net::PortalRouter router = sc::app::make_shell_router(options);
    sc::net::HttpServer server(router);
    if (!server.listen(static_cast<sc::u16>(port))) {
        std::fprintf(stderr, "shell: %s\n", server.error().c_str());
        return 1;
    }

    const unsigned bound = static_cast<unsigned>(server.port());
    std::printf("Scan Cycle 1.0 shell\n");
    std::printf("  http://127.0.0.1:%u/\n", bound);
    std::printf("  http://127.0.0.1:%u/factory\n", bound);
    std::printf("  http://127.0.0.1:%u/api/factory\n", bound);
    std::printf("  mode: %s\n", options.teacher_mode ? "teacher entry visible" : "student entry hidden");
    std::printf("  IDE: %s\n", options.ide_url.c_str());
    if (options.teacher_mode) {
        std::printf("  portal: %s\n", options.portal_url.c_str());
    }
    std::printf("\n");
    std::fflush(stdout);

    long served = 0;
    while (requests == 0 || served < requests) {
        if (!server.serve_one()) {
            std::fprintf(stderr, "shell: %s\n", server.error().c_str());
            return 1;
        }
        ++served;
    }
    return 0;
}
