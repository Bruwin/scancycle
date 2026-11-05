#include "app/lesson_api.hpp"

#include "app/lesson_runner.hpp"
#include "lesson/grader.hpp"
#include "net/http.hpp"

#include <string>

namespace sc::app {
namespace {

net::Response error_response(int status, std::string message) {
    net::Response response;
    response.status = status;
    response.body = "{\"error\":\"" + net::json_escape(message) + "\"}";
    return response;
}

}  // namespace

net::Response grade_lesson(const std::vector<lesson::Lesson>& lessons,
                           const net::Request& request) {
    const std::string wanted = net::form_field(request.body, "lesson");
    if (wanted.empty()) {
        return error_response(400, "the form needs a lesson field");
    }

    const lesson::Lesson* chosen = nullptr;
    for (const lesson::Lesson& one : lessons) {
        if (one.id == wanted) {
            chosen = &one;
        }
    }
    if (chosen == nullptr) {
        return error_response(404, "unknown lesson '" + wanted + "'");
    }

    LessonRunConfig config;
    config.program = net::form_field(request.body, "program");

    const LessonRunResult run = run_lesson(*chosen, config);
    if (!run.ok) {
        return error_response(500, run.error);
    }

    net::Response response;
    response.body = lesson::grade_report_json(run.report);
    return response;
}

}  // namespace sc::app
