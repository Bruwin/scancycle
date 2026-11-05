#include "app/grades_page.hpp"

#include <cstddef>
#include <cstdint>
#include <ctime>
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
        page += "<table><thead><tr><th>When (UTC)</th><th>Lesson</th><th>Result</th>"
                "<th>Checks</th><th>Program</th></tr></thead><tbody>";
        for (auto entry = records.rbegin(); entry != records.rend(); ++entry) {
            const GradeRecord& record = *entry;
            page += "<tr><td>" + format_time(record.at) + "</td><td><code>" +
                    net::html_escape(record.lesson_id) + "</code></td><td class=\"" +
                    (record.passed ? "pass\">pass" : "fail\">fail") + "</td><td>" +
                    std::to_string(record.passed_count) + " of " + std::to_string(record.total) +
                    "</td><td>" +
                    (record.program.empty()
                         ? std::string("reference")
                         : std::to_string(record.program.size()) + " bytes") +
                    "</td></tr>";
        }
        page += "</tbody></table>";
    }
    page += "</main></body></html>";

    net::Response response;
    response.content_type = "text/html; charset=utf-8";
    response.body = std::move(page);
    return response;
}

}  // namespace sc::app
