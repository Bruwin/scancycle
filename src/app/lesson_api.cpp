#include "app/lesson_api.hpp"

#include "app/grade_log.hpp"
#include "app/grades_query.hpp"
#include "app/lesson_runner.hpp"
#include "lesson/grader.hpp"
#include "net/http.hpp"

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>

namespace sc::app {
namespace {

struct GradeOutcome {
    int status = 200;
    std::string error;
    std::string wanted_lesson;
    std::string program;
    std::string student;
    lesson::GradeReport report;
    const lesson::Lesson* lesson = nullptr;
};

// One submission, one run. Both routes render this, so the page and the JSON can
// never disagree about which lesson was graded or what it scored. The form
// fields are captured before any check runs, because a rejected submission still
// has to come back to the student with the program they typed.
GradeOutcome grade_submission(const std::vector<lesson::Lesson>& lessons,
                              const net::Request& request) {
    GradeOutcome outcome;
    outcome.wanted_lesson = net::form_field(request.body, "lesson");
    outcome.program = net::form_field(request.body, "program");
    outcome.student = net::form_field(request.body, "student");

    if (outcome.wanted_lesson.empty()) {
        outcome.status = 400;
        outcome.error = "the form needs a lesson field";
        return outcome;
    }

    for (const lesson::Lesson& one : lessons) {
        if (one.id == outcome.wanted_lesson) {
            outcome.lesson = &one;
        }
    }
    if (outcome.lesson == nullptr) {
        outcome.status = 404;
        outcome.error = "unknown lesson '" + outcome.wanted_lesson + "'";
        return outcome;
    }

    LessonRunConfig config;
    config.program = outcome.program;

    const LessonRunResult run = run_lesson(*outcome.lesson, config);
    if (!run.ok && !run.refused) {
        outcome.status = 500;
        outcome.error = run.error;
        return outcome;
    }
    outcome.report = run.report;
    return outcome;
}

// A full disk must not cost a student their grade, so a failed append is
// dropped here; the server's startup line names the file a teacher can check.
void record_if_asked(const GradeOutcome& outcome, GradeLog* log) {
    if (log == nullptr || outcome.status != 200) {
        return;
    }
    GradeRecord record;
    record.at = static_cast<std::int64_t>(std::time(nullptr));
    record.lesson_id = outcome.report.lesson_id;
    record.passed = outcome.report.passed();
    record.passed_count = outcome.report.passed_count;
    record.total = outcome.report.checks.size();
    record.program = outcome.program;
    record.student = outcome.student;
    // The report, not just its score: a teacher reopening last week's attempt
    // needs to see which assertion missed and what the plant did.
    record.checks.reserve(record.total);
    for (const lesson::CheckResult& check : outcome.report.checks) {
        GradeCheck stored;
        stored.passed = check.passed;
        stored.description = check.description;
        stored.detail = check.detail;
        record.checks.push_back(std::move(stored));
    }

    std::string error;
    (void)log->append(record, error);
}

net::Response json_error(int status, const std::string& message) {
    net::Response response;
    response.status = status;
    response.body = "{\"error\":\"" + net::json_escape(message) + "\"}";
    return response;
}

// The report as a teacher reads it: the verdict first, then one row per check so
// a student can see which assertion failed and what the simulation actually did.
// The submission rides along in the form below it, so fixing a near miss is an
// edit rather than a retype.
std::string report_page(const GradeOutcome& outcome, const std::vector<lesson::Lesson>& lessons,
                        const std::string& filter) {
    const lesson::GradeReport& report = outcome.report;

    std::string page = net::page_head("Scan Cycle grade");
    page += "<h1>" + net::html_escape(outcome.lesson->title) + "</h1>";
    page += "<p>Lesson <code>" + net::html_escape(report.lesson_id) + "</code>: ";
    if (report.passed()) {
        page += "all " + std::to_string(report.checks.size()) + " checks passed.";
    } else {
        page += std::to_string(report.passed_count) + " of " +
                std::to_string(report.checks.size()) + " checks passed.";
    }
    page += "</p>";
    if (!outcome.student.empty()) {
        page += "<p>Submitted by <code>" + net::html_escape(outcome.student) + "</code>.</p>";
    }
    page += "<table><thead><tr><th>Check</th><th>Result</th><th>Detail</th></tr>"
            "</thead><tbody>";

    for (const lesson::CheckResult& check : report.checks) {
        page += "<tr><td>" + net::html_escape(check.description) + "</td><td class=\"";
        page += check.passed ? "pass\">pass" : "fail\">fail";
        page += "</td><td>" + net::html_escape(check.detail) + "</td></tr>";
    }

    page += "</tbody></table><h2 class=\"no-print\">Edit and grade again</h2>";
    page += net::lesson_form(lessons, report.lesson_id, outcome.program, outcome.student, filter);
    if (!filter.empty()) {
        page += "<p class=\"no-print\"><a href=\"/grades?" + filter +
                "\">Back to the filtered class log</a></p>";
    }
    page += "<p class=\"no-print\"><a href=\"/\">Grade another lesson</a></p>"
            "</main></body></html>";
    return page;
}

// A rejection is still an answer about a submission someone wrote: the message
// explains what was wrong, and the form comes back filled in.
std::string error_page(const GradeOutcome& outcome, const std::vector<lesson::Lesson>& lessons,
                       const std::string& filter) {
    std::string page = net::page_head("Scan Cycle grade");
    page += "<h1>Could not grade</h1><p>" + net::html_escape(outcome.error) + "</p>";
    page += "<h2>Your program is still here</h2>";
    page += net::lesson_form(lessons, outcome.wanted_lesson, outcome.program, outcome.student,
                             filter);
    if (!filter.empty()) {
        page += "<p class=\"no-print\"><a href=\"/grades?" + filter +
                "\">Back to the filtered class log</a></p>";
    }
    page += "<p class=\"no-print\"><a href=\"/\">Back to the catalog</a></p>"
            "</main></body></html>";
    return page;
}

}  // namespace

net::Response grade_lesson(const std::vector<lesson::Lesson>& lessons,
                           const net::Request& request, GradeLog* log) {
    const GradeOutcome outcome = grade_submission(lessons, request);
    if (outcome.status != 200) {
        return json_error(outcome.status, outcome.error);
    }
    record_if_asked(outcome, log);

    net::Response response;
    response.body = lesson::grade_report_json(outcome.report);
    return response;
}

net::Response grade_lesson_page(const std::vector<lesson::Lesson>& lessons,
                                const net::Request& request, GradeLog* log) {
    const GradeOutcome outcome = grade_submission(lessons, request);
    record_if_asked(outcome, log);

    // The filter rides on the query, not in the submission: it says which log
    // view the student came from, so the report can lead back there.
    const std::string filter = filter_query(net::form_field(request.query, "lesson"),
                                            net::form_field(request.query, "student"));

    net::Response response;
    response.status = outcome.status;
    response.content_type = "text/html; charset=utf-8";
    response.body = outcome.status == 200 ? report_page(outcome, lessons, filter)
                                          : error_page(outcome, lessons, filter);
    return response;
}

}  // namespace sc::app
