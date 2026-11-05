#include "app/lesson_api.hpp"

#include "app/lesson_runner.hpp"
#include "lesson/grader.hpp"
#include "net/http.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace sc::app {
namespace {

struct GradeOutcome {
    int status = 200;
    std::string error;
    lesson::GradeReport report;
    const lesson::Lesson* lesson = nullptr;
};

// One submission, one run. Both routes render this, so the page and the JSON can
// never disagree about which lesson was graded or what it scored.
GradeOutcome grade_submission(const std::vector<lesson::Lesson>& lessons,
                              const net::Request& request) {
    GradeOutcome outcome;
    const std::string wanted = net::form_field(request.body, "lesson");
    if (wanted.empty()) {
        outcome.status = 400;
        outcome.error = "the form needs a lesson field";
        return outcome;
    }

    for (const lesson::Lesson& one : lessons) {
        if (one.id == wanted) {
            outcome.lesson = &one;
        }
    }
    if (outcome.lesson == nullptr) {
        outcome.status = 404;
        outcome.error = "unknown lesson '" + wanted + "'";
        return outcome;
    }

    LessonRunConfig config;
    config.program = net::form_field(request.body, "program");

    const LessonRunResult run = run_lesson(*outcome.lesson, config);
    if (!run.ok) {
        outcome.status = 500;
        outcome.error = run.error;
        return outcome;
    }
    outcome.report = run.report;
    return outcome;
}

net::Response json_error(int status, const std::string& message) {
    net::Response response;
    response.status = status;
    response.body = "{\"error\":\"" + net::json_escape(message) + "\"}";
    return response;
}

std::string page_head(std::string_view title) {
    return "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
           "<title>" +
           std::string(title) +
           "</title><style>body{font-family:system-ui,sans-serif;max-width:48rem;"
           "margin:2rem auto;padding:0 1rem}table{border-collapse:collapse}"
           "td,th{padding:.25rem .6rem;text-align:left;vertical-align:top}"
           "tr+tr td{border-top:1px solid #ddd}.pass{color:#1a7f37}"
           ".fail{color:#b42318}</style></head><body>";
}

// The report as a teacher reads it: the verdict first, then one row per check so
// a student can see which assertion failed and what the simulation actually did.
std::string report_page(const GradeOutcome& outcome) {
    const lesson::GradeReport& report = outcome.report;

    std::string page = page_head("Scan Cycle grade");
    page += "<h1>" + net::html_escape(outcome.lesson->title) + "</h1>";
    page += "<p>Lesson <code>" + net::html_escape(report.lesson_id) + "</code>: ";
    if (report.passed()) {
        page += "all " + std::to_string(report.checks.size()) + " checks passed.";
    } else {
        page += std::to_string(report.passed_count) + " of " +
                std::to_string(report.checks.size()) + " checks passed.";
    }
    page += "</p><table><thead><tr><th>Check</th><th>Result</th><th>Detail</th></tr>"
            "</thead><tbody>";

    for (const lesson::CheckResult& check : report.checks) {
        page += "<tr><td>" + net::html_escape(check.description) + "</td><td class=\"";
        page += check.passed ? "pass\">pass" : "fail\">fail";
        page += "</td><td>" + net::html_escape(check.detail) + "</td></tr>";
    }

    page += "</tbody></table><p><a href=\"/\">Grade another lesson</a></p></body></html>";
    return page;
}

std::string error_page(const std::string& message) {
    std::string page = page_head("Scan Cycle grade");
    page += "<h1>Could not grade</h1><p>" + net::html_escape(message) + "</p>";
    page += "<p><a href=\"/\">Back to the catalog</a></p></body></html>";
    return page;
}

}  // namespace

net::Response grade_lesson(const std::vector<lesson::Lesson>& lessons,
                           const net::Request& request) {
    const GradeOutcome outcome = grade_submission(lessons, request);
    if (outcome.status != 200) {
        return json_error(outcome.status, outcome.error);
    }

    net::Response response;
    response.body = lesson::grade_report_json(outcome.report);
    return response;
}

net::Response grade_lesson_page(const std::vector<lesson::Lesson>& lessons,
                                const net::Request& request) {
    const GradeOutcome outcome = grade_submission(lessons, request);

    net::Response response;
    response.status = outcome.status;
    response.content_type = "text/html; charset=utf-8";
    response.body = outcome.status == 200 ? report_page(outcome) : error_page(outcome.error);
    return response;
}

}  // namespace sc::app
