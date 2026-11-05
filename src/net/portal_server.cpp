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

std::string html_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        switch (character) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out += character;
                break;
        }
    }
    return out;
}

namespace {

// One page, no script: the catalog a teacher opens to see what the classroom
// server holds, plus a form that posts a program to the grade route. Leaving the
// textarea empty grades the lesson's own reference solution.
std::string catalog_page(const std::vector<lesson::Lesson>& lessons) {
    std::string page =
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<title>Scan Cycle portal</title></head><body>"
        "<h1>Scan Cycle teacher portal</h1>"
        "<p>Every lesson's reference program must pass its own rubric.</p><ul>";

    for (const lesson::Lesson& lesson : lessons) {
        page += "<li><strong>" + html_escape(lesson.id) + "</strong> - " +
                html_escape(lesson.title) + "</li>";
    }

    page +=
        "</ul><form method=\"post\" action=\"/grade\">"
        "<p><label>Lesson <select name=\"lesson\">";
    for (const lesson::Lesson& lesson : lessons) {
        page += "<option value=\"" + html_escape(lesson.id) + "\">" + html_escape(lesson.title) +
                "</option>";
    }
    page +=
        "</select></label></p>"
        "<p><label>Program<br><textarea name=\"program\" rows=\"12\" cols=\"60\">"
        "</textarea></label></p>"
        "<p><button type=\"submit\">Grade</button></p></form>"
        "<p><a href=\"/api/lessons\">JSON catalog</a></p></body></html>";
    return page;
}

}  // namespace

PortalRouter make_default_router(const std::vector<lesson::Lesson>& lessons) {
    PortalRouter router;

    router.route(HttpMethod::Get, "/api/health", [](const Request&) {
        Response response;
        response.body = R"({"status":"ok","service":"scancycle-portal"})";
        return response;
    });

    router.route(HttpMethod::Get, "/", [lessons](const Request&) {
        Response response;
        response.content_type = "text/html; charset=utf-8";
        response.body = catalog_page(lessons);
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
