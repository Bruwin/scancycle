#include "app/grades_page.hpp"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sc::app {
namespace {

// UTC on purpose: a classroom log that shifts twice a year is a log nobody can
// compare across terms.
std::string format_time(std::int64_t seconds) {
    const std::time_t when = static_cast<std::time_t>(seconds);
    std::tm parts{};
#if defined(_WIN32)
    gmtime_s(&parts, &when);
#else
    gmtime_r(&when, &parts);
#endif
    char text[32];
    if (std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &parts) == 0) {
        return "unknown";
    }
    return std::string(text);
}

// The suffix from /grades/<n>: digits only, counted from one, and bounded so an
// absurd suffix cannot overflow the file index.
bool parse_ordinal(std::string_view text, std::size_t& out) {
    if (text.empty() || text.size() > 18) {
        return false;
    }
    std::size_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10 + static_cast<std::size_t>(character - '0');
    }
    if (value == 0) {
        return false;
    }
    out = value;
    return true;
}

struct LessonTally {
    std::size_t attempts = 0;
    std::size_t passed = 0;
    std::int64_t latest_at = 0;
    bool latest_passed = false;
};

// A teacher scanning a term's log wants the class, not the file: how each lesson
// is going and whether anyone has touched it lately. Ordered by lesson id so the
// table does not shuffle between page loads. The rate is a whole percentage,
// rounded down, because "33%" is what a person reads off three attempts.
std::string lesson_summary(const std::vector<GradeRecord>& records) {
    std::map<std::string, LessonTally> tallies;
    for (const GradeRecord& record : records) {
        LessonTally& tally = tallies[record.lesson_id];
        ++tally.attempts;
        if (record.passed) {
            ++tally.passed;
        }
        // Records arrive oldest first, so an equal timestamp means the later
        // submission is the latest one.
        if (record.at >= tally.latest_at) {
            tally.latest_at = record.at;
            tally.latest_passed = record.passed;
        }
    }

    std::string table =
        "<h2>By lesson</h2><table><thead><tr><th>Lesson</th><th>Attempts</th>"
        "<th>Passed</th><th>Pass rate</th><th>Latest</th></tr></thead><tbody>";
    for (const auto& [lesson_id, tally] : tallies) {
        const std::size_t rate = tally.passed * 100 / tally.attempts;
        table += "<tr><td><code>" + net::html_escape(lesson_id) + "</code></td><td>" +
                 std::to_string(tally.attempts) + "</td><td>" + std::to_string(tally.passed) +
                 "</td><td>" + std::to_string(rate) + "%</td><td>" +
                 format_time(tally.latest_at) + " <span class=\"";
        table += tally.latest_passed ? "pass\">pass" : "fail\">fail";
        table += "</span></td></tr>";
    }
    return table + "</tbody></table>";
}

}  // namespace

net::Response grades_page(const GradeLog& log) {
    std::string error;
    const std::vector<GradeRecord> records = log.entries(error);

    std::string page = net::page_head("Scan Cycle grades");
    page += "<p class=\"no-print\"><a href=\"/\">Scan Cycle teacher portal</a></p>";
    page += "<h1>Graded attempts</h1>";
    page += "<p>" + std::to_string(records.size()) +
            (records.size() == 1 ? " attempt" : " attempts") + " recorded in <code>" +
            net::html_escape(log.path()) + "</code>.</p>";

    if (records.empty()) {
        page += "<p>No graded attempts yet.</p>";
    } else {
        page += lesson_summary(records);
        page += "<h2>Attempts</h2>";
        page += "<table><thead><tr><th>When (UTC)</th><th>Lesson</th><th>Result</th>"
                "<th>Checks</th><th>Program</th><th class=\"no-print\">Report</th>"
                "</tr></thead><tbody>";
        // Newest first, but the ordinal is the record's place in the file, so a
        // link keeps pointing at the same attempt as more are appended.
        for (std::size_t index = records.size(); index-- > 0;) {
            const GradeRecord& record = records[index];
            page += "<tr><td>" + format_time(record.at) + "</td><td><code>" +
                    net::html_escape(record.lesson_id) + "</code></td><td class=\"" +
                    (record.passed ? "pass\">pass" : "fail\">fail") + "</td><td>" +
                    std::to_string(record.passed_count) + " of " + std::to_string(record.total) +
                    "</td><td>" +
                    (record.program.empty()
                         ? std::string("reference")
                         : std::to_string(record.program.size()) + " bytes") +
                    "</td><td class=\"no-print\"><a href=\"/grades/" +
                    std::to_string(index + 1) + "\">report</a></td></tr>";
        }
        page += "</tbody></table>";
    }
    page += "</main></body></html>";

    net::Response response;
    response.content_type = "text/html; charset=utf-8";
    response.body = std::move(page);
    return response;
}

net::Response attempt_page(const GradeLog& log, std::string_view ordinal) {
    net::Response response;
    response.content_type = "text/html; charset=utf-8";

    std::string error;
    const std::vector<GradeRecord> records = log.entries(error);
    std::size_t wanted = 0;
    if (!parse_ordinal(ordinal, wanted) || wanted > records.size()) {
        response.status = 404;
        response.body = net::page_head("Scan Cycle attempt") +
                        "<h1>No such attempt</h1><p>The class log holds " +
                        std::to_string(records.size()) + " attempt" +
                        (records.size() == 1 ? "" : "s") +
                        ".</p><p><a href=\"/grades\">Back to the class log</a></p>"
                        "</main></body></html>";
        return response;
    }

    const GradeRecord& record = records[wanted - 1];
    std::string page = net::page_head("Scan Cycle attempt");
    page += "<p class=\"no-print\"><a href=\"/grades\">Scan Cycle class log</a></p>";
    page += "<h1>Graded attempt</h1>";
    page += "<p>Attempt " + std::to_string(wanted) + " of " + std::to_string(records.size()) +
            "; lesson <code>" + net::html_escape(record.lesson_id) + "</code>; " +
            format_time(record.at) + " UTC.</p>";
    page += "<p>Result: <span class=\"";
    page += record.passed ? "pass\">pass" : "fail\">fail";
    page += "</span>; " + std::to_string(record.passed_count) + " of " +
            std::to_string(record.total) + " checks passed.</p>";

    if (record.checks.empty()) {
        page += "<p>This attempt was recorded before per-check detail was kept.</p>";
    } else {
        page += "<h2>Checks</h2><table><thead><tr><th>Check</th><th>Result</th>"
                "<th>Detail</th></tr></thead><tbody>";
        for (const GradeCheck& check : record.checks) {
            page += "<tr><td>" + net::html_escape(check.description) + "</td><td class=\"";
            page += check.passed ? "pass\">pass" : "fail\">fail";
            page += "</td><td>" + net::html_escape(check.detail) + "</td></tr>";
        }
        page += "</tbody></table>";
    }

    page += "<h2>Program</h2>";
    if (record.program.empty()) {
        page += "<p>The lesson's reference solution was graded; no program was submitted.</p>";
    } else {
        page += "<pre>" + net::html_escape(record.program) + "</pre>";
    }
    page += "</main></body></html>";

    response.body = std::move(page);
    return response;
}

}  // namespace sc::app
