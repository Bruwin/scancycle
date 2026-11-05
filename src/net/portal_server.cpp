#include "net/portal_server.hpp"

#include "lesson/lesson.hpp"

#include <utility>

namespace sc::net {

void PortalRouter::route(HttpMethod method, std::string path, Handler handler) {
    routes_.push_back(Route{method, std::move(path), std::move(handler)});
}

void PortalRouter::route_prefix(HttpMethod method, std::string prefix, Handler handler) {
    prefix_routes_.push_back(Route{method, std::move(prefix), std::move(handler)});
}

Response PortalRouter::dispatch(const Request& request) const {
    for (const Route& entry : routes_) {
        if (entry.method == request.method && entry.path == request.path) {
            return entry.handler(request);
        }
    }
    const Route* best = nullptr;
    for (const Route& entry : prefix_routes_) {
        if (entry.method == request.method && request.path.rfind(entry.path, 0) == 0 &&
            (best == nullptr || entry.path.size() > best->path.size())) {
            best = &entry;
        }
    }
    if (best != nullptr) {
        return best->handler(request);
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

// One shell for every page. The catalog, a lesson and a grade are read side by
// side, so they share type, spacing, table rules and code styling rather than
// each inventing its own.
std::string page_head(std::string_view title) {
    return "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
           "<title>" +
           std::string(title) +
           "</title><style>"
           "body{font-family:system-ui,sans-serif;line-height:1.55;max-width:52rem;"
           "margin:2rem auto;padding:0 1rem;color:#1b1b1b}"
           "table{border-collapse:collapse;margin:1rem 0;width:100%}"
           "th,td{padding:.35rem .7rem;text-align:left;vertical-align:top;"
           "border-bottom:1px solid #e2e2e2}th{background:#f6f6f6;font-weight:600}"
           "code{background:#f2f2f2;padding:.08rem .3rem;border-radius:3px}"
           "textarea{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;"
           "width:100%;box-sizing:border-box}"
           "@media print{body{margin:0;max-width:none}form,.no-print{display:none}"
           "a{color:#000;text-decoration:none}th,pre{background:none}"
           "tr{page-break-inside:avoid}thead{display:table-header-group}}"
           "pre{background:#f6f6f6;padding:.7rem;border-radius:3px;"
           "white-space:pre-wrap;overflow-wrap:anywhere}"
           ".pass{color:#1a7f37}.fail{color:#b42318}"
           "</style></head><body><main>";
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
    std::string page = page_head("Scan Cycle portal");
    page += "<h1>Scan Cycle teacher portal</h1>"
            "<p>Every lesson's reference program must pass its own rubric.</p>"
            "<table><thead><tr><th>Lesson</th><th>Id</th></tr></thead><tbody>";

    for (const lesson::Lesson& lesson : lessons) {
        page += "<tr><td><a href=\"/lessons/" + html_escape(lesson.id) + "\">" +
                html_escape(lesson.title) + "</a></td><td><code>" + html_escape(lesson.id) +
                "</code></td></tr>";
    }

    page += "</tbody></table><h2 class=\"no-print\">Grade a program</h2>";
    page += lesson_form(lessons, "", "");
    page += "<p class=\"no-print\"><a href=\"/api/lessons\">JSON catalog</a></p>"
            "</main></body></html>";
    return page;
}

// A lesson grants a mix of mnemonics, operators and type names. Grouping them
// by what they are keeps the list readable; the classification is presentation,
// so it lives here rather than becoming a second thing a lesson has to declare.
enum class TokenKind : u8 { Instruction = 0, Operator, DataType };

TokenKind token_kind(std::string_view token) {
    static constexpr std::string_view kTypes[] = {"BOOL", "INT",   "DINT", "REAL",
                                                  "TIME", "WORD",  "DWORD", "STRING"};
    static constexpr std::string_view kOperators[] = {":=", "AND", "OR",  "NOT", "XOR", "=",
                                                      "<>", "<",   ">",   "<=",  ">=",  "+",
                                                      "-",  "*",   "/"};
    for (const std::string_view& one : kTypes) {
        if (token == one) {
            return TokenKind::DataType;
        }
    }
    for (const std::string_view& one : kOperators) {
        if (token == one) {
            return TokenKind::Operator;
        }
    }
    return TokenKind::Instruction;
}

std::string toolbox_row(std::string_view label, const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        return std::string();
    }
    std::string row = "<tr><td>" + std::string(label) + "</td><td>";
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0) {
            row += ", ";
        }
        row += "<code>" + html_escape(tokens[i]) + "</code>";
    }
    return row + "</td></tr>";
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
    std::string page = page_head(lesson.title);
    page += "<p class=\"no-print\"><a href=\"/\">Scan Cycle teacher portal</a></p>";
    page += "<h1>" + html_escape(lesson.title) + "</h1>";
    page += "<p>" + html_escape(lesson.objective) + "</p><h2>Toolbox</h2>";

    if (lesson.allowed_instructions.empty()) {
        page += "<p>Every instruction is available.</p>";
    } else {
        std::vector<std::string> instructions;
        std::vector<std::string> operators;
        std::vector<std::string> types;
        for (const std::string& token : lesson.allowed_instructions) {
            switch (token_kind(token)) {
                case TokenKind::Operator:
                    operators.push_back(token);
                    break;
                case TokenKind::DataType:
                    types.push_back(token);
                    break;
                case TokenKind::Instruction:
                    instructions.push_back(token);
                    break;
            }
        }
        page += "<table><thead><tr><th>Kind</th><th>Granted</th></tr></thead><tbody>";
        page += toolbox_row("Instruction", instructions);
        page += toolbox_row("Operator", operators);
        page += toolbox_row("Data type", types);
        page += "</tbody></table>";
        page += "<p>Grading refuses a program that uses anything else.</p>";
    }

    page += "<h2>How this is graded</h2><table><thead><tr><th>Check</th>"
                       "<th>What passes</th></tr></thead><tbody>";
    for (const lesson::RubricCheck& check : lesson.rubric) {
        page += "<tr><td>" + html_escape(check.description) + "</td><td>" +
                html_escape(rule_text(check, lesson.starting_plant)) + "</td></tr>";
    }
    page += "</tbody></table><h2>Submit a program</h2>";
    page += lesson_form(lessons, lesson.id, "");
    page += "</main></body></html>";
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
