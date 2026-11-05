#include "lesson/lesson.hpp"
#include "net/portal_server.hpp"

#include <cstdio>
#include <vector>

// The teacher portal transport is M5 work. This binary exercises the routing
// layer that the transport will serve, so the classroom surface can be reviewed
// and tested before any port is opened.
int main() {
    std::vector<sc::lesson::Lesson> lessons = sc::lesson::make_builtin_lessons();

    sc::net::PortalRouter router = sc::net::make_default_router(lessons);

    std::printf("Scan Cycle teacher portal (routing layer, transport pending)\n");
    std::printf("routes: %zu\n\n", router.route_count());

    const char* paths[] = {"/api/health", "/api/lessons", "/api/unknown"};
    for (const char* path : paths) {
        sc::net::Request request;
        request.method = sc::net::HttpMethod::Get;
        request.path = path;

        const sc::net::Response response = router.dispatch(request);
        std::printf("GET %-14s -> %d %s\n", path, response.status, response.body.c_str());
    }
    return 0;
}
