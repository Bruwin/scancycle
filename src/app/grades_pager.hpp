#pragma once
// Paging for the class log. The attempt table is browsed a page at a time while
// the summaries keep describing the whole filtered set: a rate that changes when
// the reader turns a page is not a rate.
#include <cstddef>
#include <string>
#include <string_view>

namespace sc::app {

inline constexpr std::size_t kAttemptsPerPage = 25;

// How many pages a filtered set needs. An empty log still counts as one page.
std::size_t attempt_page_count(std::size_t attempts);

// A /grades link for one page, carrying the filter and omitting `page=1`, so a
// view that never paged keeps the URL the rest of the portal already links to.
std::string log_page_href(std::size_t page, std::string_view lesson, std::string_view student,
                          std::string_view result = {});

// The page line, or nothing when everything fits on one page.
std::string attempt_pager(std::size_t page, std::size_t pages, std::string_view lesson,
                          std::string_view student, std::string_view result = {});

// A page number from a query string. Anything unusable means page one: a
// truncated link should show the log, not an error.
std::size_t parse_attempt_page(std::string_view text);

}  // namespace sc::app
