#include "app/grades_students.hpp"

#include "app/grades_query.hpp"
#include "app/grades_time.hpp"
#include "net/portal_server.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sc::app {
namespace {

struct StudentTally {
    std::size_t attempts = 0;
    std::size_t passed = 0;
    std::int64_t latest_at = 0;
    bool latest_passed = false;
};

void add_attempt(const GradeRecord& record, StudentTally& tally) {
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

// `cell` is ready markup: a link for a named student, a plain label otherwise.
std::string row(const std::string& cell, const StudentTally& tally) {
    const std::size_t rate = tally.passed * 100 / tally.attempts;
    std::string out = "<tr><td>" + cell + "</td><td>" + std::to_string(tally.attempts) +
                      "</td><td>" + std::to_string(tally.passed) + "</td><td>" +
                      std::to_string(rate) + "%</td><td>" + format_utc_minutes(tally.latest_at) +
                      " <span class=\"";
    out += tally.latest_passed ? "pass\">pass" : "fail\">fail";
    out += "</span></td></tr>";
    return out;
}

}  // namespace

std::string student_summary(const std::vector<const GradeRecord*>& records) {
    std::map<std::string, StudentTally> named;
    StudentTally unnamed;
    bool has_unnamed = false;
    for (const GradeRecord* record : records) {
        if (record->student.empty()) {
            add_attempt(*record, unnamed);
            has_unnamed = true;
            continue;
        }
        add_attempt(*record, named[record->student]);
    }

    if (named.empty()) {
        return std::string();
    }

    std::string table = "<h2>By student</h2><table><thead><tr><th>Student</th>"
                        "<th>Attempts</th><th>Passed</th><th>Pass rate</th><th>Latest</th>"
                        "</tr></thead><tbody>";
    for (const auto& [name, tally] : named) {
        table += row("<a href=\"/grades?student=" + query_escape(name) + "\">" +
                         net::html_escape(name) + "</a>",
                     tally);
    }
    if (has_unnamed) {
        // Last, and labelled as a gap: folding anonymous attempts into a student
        // would overstate that student's work, and dropping them would overstate
        // the class's pass rate.
        table += row("<em>(unnamed)</em>", unnamed);
    }
    return table + "</tbody></table>";
}

}  // namespace sc::app
