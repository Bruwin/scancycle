#include "app/grades_pager.hpp"

#include "app/grades_query.hpp"

#include <charconv>
#include <system_error>

namespace sc::app {

std::size_t attempt_page_count(std::size_t attempts) {
    const std::size_t pages = (attempts + kAttemptsPerPage - 1) / kAttemptsPerPage;
    return pages == 0 ? 1 : pages;
}

std::string log_page_href(std::size_t page, std::string_view lesson, std::string_view student,
                          std::string_view result) {
    std::string query = filter_query(lesson, student, result);
    if (page > 1) {
        if (!query.empty()) {
            query += "&";
        }
        query += "page=" + std::to_string(page);
    }
    return query.empty() ? std::string("/grades") : "/grades?" + query;
}

std::string attempt_pager(std::size_t page, std::size_t pages, std::string_view lesson,
                          std::string_view student, std::string_view result) {
    if (pages <= 1) {
        return {};
    }
    std::string pager = "<p class=\"no-print\">Page " + std::to_string(page) + " of " +
                        std::to_string(pages);
    if (page > 1) {
        pager += " | <a href=\"" + log_page_href(page - 1, lesson, student, result) +
                 "\">Newer</a>";
    }
    if (page < pages) {
        pager += " | <a href=\"" + log_page_href(page + 1, lesson, student, result) +
                 "\">Older</a>";
    }
    pager += "</p>";
    return pager;
}

std::size_t parse_attempt_page(std::string_view text) {
    if (text.empty()) {
        return 1;
    }
    std::size_t page = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, page);
    if (parsed.ec != std::errc() || parsed.ptr != end || page == 0) {
        return 1;
    }
    return page;
}

}  // namespace sc::app
