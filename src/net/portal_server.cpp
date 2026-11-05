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

std::string lesson_form(const std::vector<lesson::Lesson>& lessons, std::string_view selected,
                        std::string_view program) {
    std::string form =
        "<form method=\"post\" action=\"/grade\">"
        "<p><label>Lesson <select name=\"lesson\">";
    for (const lesson::Lesson& lesson : lessons) {
        form += "<option value=\"" + html_escape(lesson.id) + "\"";
        if (lesson.id == selected) {
            form += " selected";
        }
        form += ">" + html_escape(lesson.title) + "</option>";
    }
    form +=
        "</select></label></p>"
        "<p><label>Program<br><textarea name=\"program\" rows=\"12\" cols=\"60\">";
    form += html_escape(program);
    form +=
        "</textarea></label></p>"
        "<p><button type=\"submit\">Grade</button></p></form>";
    return form;
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
        page += "<li><a href=\"/lessons/" + html_escape(lesson.id) + "\">" +
                html_escape(lesson.title) + "</a> <code>" + html_escape(lesson.id) +
                "</code></li>";
    }

    page += "</ul>";
    page += lesson_form(lessons, "", "");
    page += "<p><a href=\"/api/lessons\">JSON catalog</a></p></body></html>";
    return page;
}

std::string number_text(f64 value) {
    const long long whole = static_cast<long long>(value);
    if (static_cast<f64>(whole) == value) {
        return std::to_string(whole);
    }
    return std::to_string(value);
}

// What the check actually measures, in the sentence a student would say. The
// rubric's own description is the goal; this is the measurement.
std::string rule_text(const lesson::RubricCheck& check, const sim::Plant& plant) {
    std::string prefix;
    if (check.kind != lesson::CheckKind::ProgramHealthy) {
        prefix = check.device < plant.count() ? plant.device(check.device).name + ": "
                                              : "device " + std::to_string(check.device) + ": ";
    }
    switch (check.kind) {
        case lesson::CheckKind::DeviceOutputTrue:
            return prefix + "output on";
        case lesson::CheckKind::DeviceOutputFalse:
            return prefix + "output off";
        case lesson::CheckKind::BoxCountAtLeast:
            return prefix + "at least " + number_text(check.threshold) + " boxes";
        case lesson::CheckKind::SpeedAtLeast:
            return prefix + "speed at least " + number_text(check.threshold);
        case lesson::CheckKind::ProcessedCountAtLeast:
            return prefix + "at least " + number_text(check.threshold) + " parts processed";
        case lesson::CheckKind::ProgramHealthy:
            return "program runs without a fault";
    }
    return "the check passes";
}

// A lesson before anyone submits: the goal, the rubric it will be measured
// against, and the form, so a student reads the rules instead of guessing them.
std::string lesson_page(const lesson::Lesson& lesson, const std::vector<lesson::Lesson>& lessons) {
    std::string page = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                       "<title>" +
                       html_escape(lesson.title) +
                       "</title></head><body>"
                       "<p><a href=\"/\">Scan Cycle teacher portal</a></p><h1>" +
                       html_escape(lesson.title) + "</h1><p>" +
                       html_escape(lesson.objective) + "</p><h2>Toolbox</h2>";

    if (lesson.allowed_instructions.empty()) {
        page += "<p>Every instruction is available.</p>";
    } else {
        page += "<p>This lesson grants: ";
        for (std::size_t i = 0; i < lesson.allowed_instructions.size(); ++i) {
            if (i != 0) {
                page += ", ";
            }
            page += "<code>" + html_escape(lesson.allowed_instructions[i]) + "</code>";
        }
        page += "</p>";
    }

    page += "<h2>How this is graded</h2><table><thead><tr><th>Check</th>"
                       "<th>What passes</th></tr></thead><tbody>";
    for (const lesson::RubricCheck& check : lesson.rubric) {
        page += "<tr><td>" + html_escape(check.description) + "</td><td>" +
                html_escape(rule_text(check, lesson.starting_plant)) + "</td></tr>";
    }
    page += "</tbody></table><h2>Submit a program</h2>";
    page += lesson_form(lessons, lesson.id, "");
    page += "</body></html>";
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

    // A page per lesson, registered from the list the server was handed so a
    // lesson cannot be in the catalog and missing from the router.
    for (std::size_t i = 0; i < lessons.size(); ++i) {
        router.route(HttpMethod::Get, "/lessons/" + lessons[i].id,
                     [lessons, i](const Request&) {
                         Response response;
                         response.content_type = "text/html; charset=utf-8";
                         response.body = lesson_page(lessons[i], lessons);
                         return response;
                     });
    }

    return router;
}

}  // namespace sc::net
