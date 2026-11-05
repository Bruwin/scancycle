#include "net/portal_server.hpp"

#include "lesson/lesson.hpp"

#include <utility>

namespace sc::net {

void PortalRouter::route(HttpMethod method, std::string path, Handler handler) {
    routes_.push_back(Route{method, std::move(path), std::move(handler)});
}

Response PortalRouter::dispatch(const Request& request) const {
    for (const Route& entry : routes_) {
        if (entry.method == request.method && entry.path == request.path) {
            return entry.handler(request);
        }
    }
    Response missing;
    missing.status = 404;
    missing.body = R"({"error":"not found"})";
    return missing;
}

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char character : text) {
        switch (character) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            default:
                out += character;
                break;
        }
    }
    return out;
}

PortalRouter make_default_router(const std::vector<lesson::Lesson>& lessons) {
    PortalRouter router;

    router.route(HttpMethod::Get, "/api/health", [](const Request&) {
        Response response;
        response.body = R"({"status":"ok","service":"scancycle-portal"})";
        return response;
    });

    router.route(HttpMethod::Get, "/api/lessons", [lessons](const Request&) {
        std::string body = "{\"lessons\":[";
        for (std::size_t i = 0; i < lessons.size(); ++i) {
            if (i != 0) {
                body += ',';
            }
            body += "{\"id\":\"" + json_escape(lessons[i].id) + "\",\"title\":\"" +
                    json_escape(lessons[i].title) + "\"}";
        }
        body += "]}";
        Response response;
        response.body = std::move(body);
        return response;
    });

    return router;
}

}  // namespace sc::net
