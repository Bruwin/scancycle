#include "app/grades_coverage.hpp"

#include "app/grades_query.hpp"
#include "net/portal_server.hpp"

#include <cstddef>
#include <map>
#include <span>
#include <string>

namespace sc::app {
namespace {

struct Coverage {
    std::size_t attempts = 0;
    std::size_t passed = 0;
};

}  // namespace

std::string student_coverage(const std::vector<const GradeRecord*>& records,
                             std::span<const std::string> catalog) {
    // Nested maps keep both axes sorted, so the table does not reorder itself
    // between loads as attempts arrive. Columns start as the catalog, because an
    // untouched lesson is the gap a teacher most needs to see, and any log-only
    // lesson is added as encounters it.
    std::map<std::string, std::map<std::string, Coverage>> coverage;
    std::map<std::string, bool> lessons;
    for (const std::string& lesson : catalog) {
        if (!lesson.empty()) {
            lessons[lesson] = true;
        }
    }
    for (const GradeRecord* record : records) {
        if (record->student.empty()) {
            continue;
        }
        Coverage& cell = coverage[record->student][record->lesson_id];
        ++cell.attempts;
        if (record->passed) {
            ++cell.passed;
        }
        lessons[record->lesson_id] = true;
    }
    if (coverage.empty()) {
        return {};
    }

    std::string table =
        "<h2>Coverage by student and lesson</h2>"
        "<p class=\"no-print\">Each cell is passed attempts over attempts; a dash means the "
        "student has not tried that lesson.</p>"
        "<table><thead><tr><th>Student</th>";
    for (const auto& lesson : lessons) {
        table += "<th><code>" + net::html_escape(lesson.first) + "</code></th>";
    }
    table += "</tr></thead><tbody>";

    for (const auto& student : coverage) {
        table += "<tr><th><a href=\"/grades?student=" + query_escape(student.first) + "\">" +
                 net::html_escape(student.first) + "</a></th>";
        for (const auto& lesson : lessons) {
            const auto cell = student.second.find(lesson.first);
            if (cell == student.second.end()) {
                table += "<td>-</td>";
                continue;
            }
            table += "<td><a href=\"/grades?student=" + query_escape(student.first) +
                     "&lesson=" + query_escape(lesson.first) + "\">" +
                     std::to_string(cell->second.passed) + "/" +
                     std::to_string(cell->second.attempts) + "</a></td>";
        }
        table += "</tr>";
    }
    return table + "</tbody></table>";
}

}  // namespace sc::app
