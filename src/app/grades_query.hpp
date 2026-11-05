#pragma once

#include <cstdio>
#include <string>
#include <string_view>

namespace sc::app {

// A lesson id or student name comes from the log, which is a text file a person
// can edit, so a filter link is built from an escaped value rather than pasted in
// verbatim. Only the unreserved characters survive unchanged.
inline std::string query_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        const bool plain = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9') || character == '-' ||
                           character == '_';
        if (plain) {
            out.push_back(character);
        } else {
            char encoded[4];
            std::snprintf(encoded, sizeof(encoded), "%%%02X",
                          static_cast<unsigned>(static_cast<unsigned char>(character)));
            out += encoded;
        }
    }
    return out;
}

// The query string that reproduces a filter, without the leading '?'. Empty when
// nothing is filtered, so callers can leave the URL bare instead of adding '?'.
inline std::string filter_query(std::string_view lesson, std::string_view student,
                                std::string_view result = {}) {
    std::string query;
    if (!lesson.empty()) {
        query += "lesson=" + query_escape(lesson);
    }
    if (!student.empty()) {
        if (!query.empty()) {
            query += "&";
        }
        query += "student=" + query_escape(student);
    }
    if (!result.empty()) {
        if (!query.empty()) {
            query += "&";
        }
        query += "result=" + query_escape(result);
    }
    return query;
}

// `?result=` is `pass` or `fail`; empty means either. Anything else matches
// nothing, which keeps an unknown value an empty page rather than a silent
// synonym for "unfiltered".
inline bool matches_result(bool passed, std::string_view result) {
    if (result.empty()) {
        return true;
    }
    if (result == "pass") {
        return passed;
    }
    if (result == "fail") {
        return !passed;
    }
    return false;
}

}  // namespace sc::app
