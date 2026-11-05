#include "lesson/grader.hpp"

#include <string>
#include <string_view>

namespace sc::lesson {
namespace {

// The report's fields are ours, but a lesson id or a detail can grow quotes, so
// the escaping is not optional just because no current lesson needs it.
std::string escape_json(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char character : text) {
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

}  // namespace

std::string grade_report_json(const GradeReport& report) {
    std::string out = "{\"lesson\":\"" + escape_json(report.lesson_id) + "\",\"passed\":";
    out += report.passed() ? "true" : "false";
    out += ",\"passed_count\":" + std::to_string(report.passed_count);
    out += ",\"checks\":[";

    for (std::size_t i = 0; i < report.checks.size(); ++i) {
        if (i != 0) {
            out += ',';
        }
        const CheckResult& check = report.checks[i];
        out += "{\"id\":\"" + escape_json(check.id) + "\",\"description\":\"" +
               escape_json(check.description) + "\",\"passed\":";
        out += check.passed ? "true" : "false";
        out += ",\"detail\":\"" + escape_json(check.detail) + "\"}";
    }

    out += "]}";
    return out;
}

}  // namespace sc::lesson
