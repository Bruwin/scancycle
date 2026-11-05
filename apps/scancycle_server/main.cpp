#include "app/lesson_api.hpp"
#include "lesson/lesson.hpp"
#include "net/http_server.hpp"
#include "net/portal_server.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// The teacher portal. The router is pure request-to-response mapping and lives
// in sc_net; this binary binds it to loopback and prints the addresses a teacher
// can open. Ctrl+C stops it.
int main(int argc, char** argv) {
    unsigned port = 8080;
    long requests = 0;  // 0 serves until the process is stopped

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--port=", 0) == 0) {
            const long value = std::strtol(arg.c_str() + 7, nullptr, 10);
            if (value <= 0 || value > 65535) {
                std::fprintf(stderr, "portal: '%s' is not a port\n", arg.c_str());
                return 2;
            }
            port = static_cast<unsigned>(value);
        } else if (arg.rfind("--requests=", 0) == 0) {
            requests = std::strtol(arg.c_str() + 11, nullptr, 10);
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    const std::vector<sc::lesson::Lesson> lessons = sc::lesson::make_builtin_lessons();
    sc::net::PortalRouter router = sc::net::make_default_router(lessons);
    router.route(sc::net::HttpMethod::Post, "/api/grade",
                 [&lessons](const sc::net::Request& request) {
                     return sc::app::grade_lesson(lessons, request);
                 });

    router.route(sc::net::HttpMethod::Post, "/grade",
                 [&lessons](const sc::net::Request& request) {
                     return sc::app::grade_lesson_page(lessons, request);
                 });

    sc::net::HttpServer server(router);
    if (!server.listen(static_cast<sc::u16>(port))) {
        std::fprintf(stderr, "portal: %s\n", server.error().c_str());
        return 1;
    }

    const unsigned bound = static_cast<unsigned>(server.port());
    std::printf("Scan Cycle teacher portal\n");
    std::printf("  http://127.0.0.1:%u/\n", bound);
    std::printf("  http://127.0.0.1:%u/api/health\n", bound);
    std::printf("  http://127.0.0.1:%u/api/lessons\n", bound);
    std::printf("  POST http://127.0.0.1:%u/api/grade  (JSON)\n", bound);
    std::printf("  POST http://127.0.0.1:%u/grade      (page)\n\n", bound);
    for (const sc::lesson::Lesson& lesson : lessons) {
        std::printf("  lesson %-20s %s\n", lesson.id.c_str(), lesson.title.c_str());
    }
    std::printf("\n");
    std::fflush(stdout);

    long served = 0;
    while (requests == 0 || served < requests) {
        if (!server.serve_one()) {
            std::fprintf(stderr, "portal: %s\n", server.error().c_str());
            return 1;
        }
        ++served;
    }
    return 0;
}
