#include "app/grades_csv.hpp"

#include "app/grades_time.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace sc::app {
namespace {

// RFC 4180 quoting. Line breaks and tabs are flattened instead of quoted: a
// gradebook is one line per attempt, and some tools count rows that way even
// when a quoted cell spans lines.
std::string csv_field(const std::string& text) {
    std::string clean;
    clean.reserve(text.size());
    bool needs_quotes = false;
    for (const char character : text) {
        if (character == '\n' || character == '\r' || character == '\t') {
            clean.push_back(' ');
        } else {
            clean.push_back(character);
        }
        if (character == ',' || character == '"') {
            needs_quotes = true;
        }
    }
    if (!needs_quotes) {
        return clean;
    }

    std::string quoted = "\"";
    for (const char character : clean) {
        if (character == '"') {
            quoted += "\"\"";
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back('"');
    return quoted;
}

}  // namespace

net::Response grades_csv(const GradeLog& log, std::string_view lesson, std::string_view student) {
    std::string error;
    const std::vector<GradeRecord> records = log.entries(error);

    std::string csv = "attempt,when_utc,student,lesson,result,passed,total,missed_checks\n";
    for (std::size_t index = 0; index < records.size(); ++index) {
        const GradeRecord& record = records[index];
        if ((!lesson.empty() && record.lesson_id != lesson) ||
            (!student.empty() && record.student != student)) {
            continue;
        }
        std::string missed;
        for (const GradeCheck& check : record.checks) {
            if (check.passed) {
                continue;
            }
            if (!missed.empty()) {
                missed += "; ";
            }
            missed += check.description;
        }

        csv += std::to_string(index + 1) + "," + csv_field(format_utc_minutes(record.at)) + "," +
               csv_field(record.student) + "," + csv_field(record.lesson_id) + "," +
               (record.passed ? "pass" : "fail") + "," +
               std::to_string(record.passed_count) + "," + std::to_string(record.total) + "," +
               csv_field(missed) + "\n";
    }

    net::Response response;
    response.content_type = "text/csv; charset=utf-8";
    response.body = std::move(csv);
    return response;
}

}  // namespace sc::app
