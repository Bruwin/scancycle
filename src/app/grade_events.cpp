#include "app/grade_events.hpp"

#include "net/portal_server.hpp"

#include <string>

namespace sc::app {

std::string grade_recorded_json(const GradeRecord& record) {
    std::string json = "{\"type\":\"grade\",\"lesson\":\"";
    json += net::json_escape(record.lesson_id);
    json += "\",\"student\":\"";
    json += net::json_escape(record.student);
    json += "\",\"passed\":";
    json += record.passed ? "true" : "false";
    json += ",\"passed_count\":";
    json += std::to_string(record.passed_count);
    json += ",\"total\":";
    json += std::to_string(record.total);
    json += ",\"at\":";
    json += std::to_string(record.at);
    json += '}';
    return json;
}

std::string term_changed_json(std::string_view term) {
    std::string json = "{\"type\":\"term\",\"term\":\"";
    json += net::json_escape(term);
    json += "\"}";
    return json;
}

}  // namespace sc::app
