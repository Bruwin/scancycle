#include "app/grades_page.hpp"

#include "app/grades_coverage.hpp"
#include "app/grades_pager.hpp"
#include "app/grades_query.hpp"
#include "app/grades_students.hpp"
#include "app/grades_time.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sc::app {
namespace {

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
std::string lesson_summary(const std::vector<const GradeRecord*>& records) {
    std::map<std::string, LessonTally> tallies;
    for (const GradeRecord* record : records) {
        LessonTally& tally = tallies[record->lesson_id];
        ++tally.attempts;
        if (record->passed) {
            ++tally.passed;
        }
        // Records arrive oldest first, so an equal timestamp means the later
        // submission is the latest one.
        if (record->at >= tally.latest_at) {
            tally.latest_at = record->at;
            tally.latest_passed = record->passed;
        }
    }

    std::string table =
        "<h2>By lesson</h2><table><thead><tr><th>Lesson</th><th>Attempts</th>"
        "<th>Passed</th><th>Pass rate</th><th>Latest</th></tr></thead><tbody>";
    for (const auto& [lesson_id, tally] : tallies) {
        const std::size_t rate = tally.passed * 100 / tally.attempts;
        table += "<tr><td><a href=\"/grades?lesson=" + query_escape(lesson_id) + "\"><code>" +
                 net::html_escape(lesson_id) + "</code></a></td><td>" +
                 std::to_string(tally.attempts) + "</td><td>" + std::to_string(tally.passed) +
                 "</td><td>" + std::to_string(rate) + "%</td><td>" +
                 format_utc_minutes(tally.latest_at) + " <span class=\"";
        table += tally.latest_passed ? "pass\">pass" : "fail\">fail";
        table += "</span></td></tr>";
    }
    return table + "</tbody></table>";
}

struct CheckTally {
    std::size_t seen = 0;
    std::size_t missed = 0;
};

// Which rubric rows the class keeps tripping over. Keyed by lesson as well as by
// wording, because two lessons can phrase a check the same way and expect
// different things. Only misses are listed: a check everyone passes is not a
// teaching problem, and listing it would bury the ones that are.
std::string missed_checks(const std::vector<const GradeRecord*>& records) {
    using Key = std::pair<std::string, std::string>;
    std::map<Key, CheckTally> tallies;
    for (const GradeRecord* record : records) {
        for (const GradeCheck& check : record->checks) {
            CheckTally& tally = tallies[{record->lesson_id, check.description}];
            ++tally.seen;
            if (!check.passed) {
                ++tally.missed;
            }
        }
    }

    const std::string heading = "<h2>Missed checks</h2>";
    std::vector<std::pair<Key, CheckTally>> rows;
    for (const auto& entry : tallies) {
        if (entry.second.missed > 0) {
            rows.push_back(entry);
        }
    }
    if (rows.empty()) {
        if (tallies.empty()) {
            return heading +
                   "<p>These attempts were recorded without per-check detail, so there is "
                   "nothing to total.</p>";
        }
        return heading + "<p>No check has been missed in the attempts kept here.</p>";
    }

    // Most-missed first. A stable sort keeps the map's lesson-then-wording order
    // for ties, so the table does not shuffle between reloads.
    std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
        return left.second.missed > right.second.missed;
    });

    std::string table = heading +
                        "<table><thead><tr><th>Lesson</th><th>Check</th><th>Missed</th>"
                        "<th>Seen</th><th>Miss rate</th></tr></thead><tbody>";
    for (const auto& row : rows) {
        const std::size_t rate = row.second.missed * 100 / row.second.seen;
        table += "<tr><td><code>" + net::html_escape(row.first.first) + "</code></td><td>" +
                 net::html_escape(row.first.second) + "</td><td>" +
                 std::to_string(row.second.missed) + "</td><td>" +
                 std::to_string(row.second.seen) + "</td><td>" + std::to_string(rate) +
                 "%</td></tr>";
    }
    return table + "</tbody></table>";
}

}  // namespace

net::Response grades_page(const GradeLog& log, std::string_view lesson, std::string_view student,
                          std::size_t page_number, std::span<const std::string> catalog,
                          std::string_view result) {
    std::string error;
    const std::vector<GradeRecord> records = log.entries(error);

    // Pointers, not copies: a filtered page needs the records that match but the
    // log already owns the programs, and copying them would double the memory a
    // term's log costs for nothing.
    std::vector<const GradeRecord*> shown;
    shown.reserve(records.size());
    for (const GradeRecord& record : records) {
        if ((lesson.empty() || record.lesson_id == lesson) &&
            (student.empty() || record.student == student) &&
            matches_result(record.passed, result)) {
            shown.push_back(&record);
        }
    }

    const bool filtered = !lesson.empty() || !student.empty() || !result.empty();
    std::string page = net::page_head("Scan Cycle grades");
    page += "<p class=\"no-print\"><a href=\"/\">Scan Cycle teacher portal</a></p>";
    page += "<h1>Graded attempts</h1>";
    if (!filtered) {
        page += "<p>" + std::to_string(records.size()) +
                (records.size() == 1 ? " attempt" : " attempts") + " recorded in <code>" +
                net::html_escape(log.path()) + "</code>.</p>";
    } else {
        page += "<p>" + std::to_string(shown.size()) +
                (shown.size() == 1 ? " attempt" : " attempts");
        if (!student.empty()) {
            page += " by <code>" + net::html_escape(student) + "</code>";
        }
        if (!lesson.empty()) {
            page += " for lesson <code>" + net::html_escape(lesson) + "</code>";
        }
        if (!result.empty()) {
            page += " with result <code>" + net::html_escape(result) + "</code>";
        }
        page += " in <code>" + net::html_escape(log.path()) + "</code>.</p>";
        page += "<p class=\"no-print\"><a href=\"/grades\">Show the whole log</a></p>";
    }

    if (records.empty()) {
        page += "<p>No graded attempts yet.</p>";
    } else if (shown.empty()) {
        page += "<p>No attempts recorded";
        if (!student.empty()) {
            page += " by <code>" + net::html_escape(student) + "</code>";
        }
        if (!lesson.empty()) {
            page += " for lesson <code>" + net::html_escape(lesson) + "</code>";
        }
        if (!result.empty()) {
            page += " with result <code>" + net::html_escape(result) + "</code>";
        }
        page += ".</p>";
    } else {
        const std::string query = filter_query(lesson, student, result);
        const std::string csv_href = query.empty() ? std::string("/grades.csv")
                                                   : "/grades.csv?" + query;
        // Paging slices the table only. The summaries still count every filtered
        // attempt, because a rate that changes per page is not a rate.
        const std::size_t pages = attempt_page_count(shown.size());
        const std::size_t current = page_number == 0 ? 1 : std::min(page_number, pages);
        const std::size_t first = (current - 1) * kAttemptsPerPage;
        const std::size_t stop = std::min(first + kAttemptsPerPage, shown.size());
        page += "<p class=\"no-print\"><a href=\"" + csv_href +
                "\" download>Download as CSV</a></p>";
        page += lesson_summary(shown);
        page += student_summary(shown);
        page += student_coverage(shown, catalog);
        page += missed_checks(shown);
        page += "<h2>Attempts</h2>";
        page += attempt_pager(current, pages, lesson, student, result);
        page += "<table><thead><tr><th>When (UTC)</th><th>Student</th><th>Lesson</th>"
                "<th>Result</th><th>Checks</th><th>Program</th>"
                "<th class=\"no-print\">Report</th></tr></thead><tbody>";
        // Newest first, but the ordinal is the record's place in the file, so a
        // link keeps pointing at the same attempt as more are appended. A filter
        // hides rows and a page slices them; neither renumbers anything.
        for (std::size_t offset = stop; offset-- > first;) {
            const GradeRecord& record = *shown[offset];
            const std::size_t ordinal =
                static_cast<std::size_t>(&record - records.data()) + 1;
            const std::string who =
                record.student.empty() ? std::string("-") : net::html_escape(record.student);
            page += "<tr><td>" + format_utc_minutes(record.at) + "</td><td>" + who +
                    "</td><td><code>" + net::html_escape(record.lesson_id) +
                    "</code></td><td class=\"" +
                    (record.passed ? "pass\">pass" : "fail\">fail") + "</td><td>" +
                    std::to_string(record.passed_count) + " of " + std::to_string(record.total) +
                    "</td><td>" +
                    (record.program.empty()
                         ? std::string("reference")
                         : std::to_string(record.program.size()) + " bytes") +
                    "</td><td class=\"no-print\"><a href=\"/grades/" +
                    std::to_string(ordinal) +
                    (query.empty() ? std::string() : "?" + query) + "\">report</a></td></tr>";
        }
        page += "</tbody></table>";
    }
    page += "</main></body></html>";

    net::Response response;
    response.content_type = "text/html; charset=utf-8";
    response.body = std::move(page);
    return response;
}

net::Response attempt_page(const GradeLog& log, std::string_view ordinal, std::string_view lesson,
                           std::string_view student, std::string_view result) {
    net::Response response;
    response.content_type = "text/html; charset=utf-8";

    // The filter belongs to the view the report was opened from, not to the
    // attempt, so it only shapes the way back.
    const std::string query = filter_query(lesson, student, result);
    const std::string back = query.empty() ? std::string("/grades") : "/grades?" + query;

    std::string error;
    const std::vector<GradeRecord> records = log.entries(error);
    std::size_t wanted = 0;
    if (!parse_ordinal(ordinal, wanted) || wanted > records.size()) {
        response.status = 404;
        response.body = net::page_head("Scan Cycle attempt") +
                        "<h1>No such attempt</h1><p>The class log holds " +
                        std::to_string(records.size()) + " attempt" +
                        (records.size() == 1 ? "" : "s") +
                        ".</p><p><a href=\"" + back +
                        "\">Back to the class log</a></p></main></body></html>";
        return response;
    }

    const GradeRecord& record = records[wanted - 1];
    std::string page = net::page_head("Scan Cycle attempt");
    page += "<p class=\"no-print\"><a href=\"" + back +
            "\">Scan Cycle class log</a></p>";
    page += "<h1>Graded attempt</h1>";
    page += "<p>Attempt " + std::to_string(wanted) + " of " + std::to_string(records.size()) +
            "; lesson <code>" + net::html_escape(record.lesson_id) + "</code>";
    if (!record.student.empty()) {
        page += "; student <code>" + net::html_escape(record.student) + "</code>";
    }
    page += "; " + format_utc_minutes(record.at) + " UTC.</p>";
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
        // A near miss is fixed by editing, not retyping, and the form carries
        // the filter so the fresh report still leads back to this view.
        page += "<h2 class=\"no-print\">Edit and grade again</h2>"
                "<form method=\"post\" action=\"/grade";
        if (!query.empty()) {
            page += "?";
            page += query;
        }
        page += "\"><input type=\"hidden\" name=\"lesson\" value=\"" +
                net::html_escape(record.lesson_id) + "\">";
        page += "<input type=\"hidden\" name=\"student\" value=\"" +
                net::html_escape(record.student) + "\">";
        page += "<p><label>Program<br><textarea name=\"program\" rows=\"12\" cols=\"60\">";
        page += net::html_escape(record.program) + "</textarea></label></p>";
        page += "<p><button type=\"submit\">Grade again</button></p></form>";
    }
    page += "</main></body></html>";

    response.body = std::move(page);
    return response;
}

}  // namespace sc::app
