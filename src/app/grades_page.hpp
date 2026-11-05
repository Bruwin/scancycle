#pragma once
// The printable class log: every recorded attempt, newest first. Kept out of
// sc_net because the on-disk format is an app concern, not a transport one.
#include "app/grade_log.hpp"
#include "net/portal_server.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace sc::app {

// `lesson` and `student` are the query fields of the same names. Empty means
// unfiltered; both empty shows the whole log, and both given narrows to that
// student's attempts at that lesson. A value nobody attempted is an empty page,
// not a 404: the log itself exists and the query is well formed. `page` is the
// 1-based `page` field: it slices the attempt table only, while the summaries and
// the CSV keep describing the whole filtered set. A page past the end shows the
// last one. `catalog` is the lesson ids the coverage table always shows as
// columns, so a lesson nobody attempted is still visible; empty would only show
// lessons that already appear in the log. `result` is `pass` or `fail`; anything
// else is an empty page, like an unknown lesson or student. `term` names the log
// being read, `terms` lists the ones a teacher can switch to and `active` is
// where new attempts go; when `terms` is empty there is no switcher and the
// portal is the single-file mode.
net::Response grades_page(const GradeLog& log, std::string_view lesson = {},
                          std::string_view student = {}, std::size_t page = 1,
                          std::span<const std::string> catalog = {},
                          std::string_view result = {}, std::string_view term = {},
                          std::span<const std::string> terms = {},
                          std::string_view active = {});

// One recorded attempt with its full report, for the page a teacher prints.
// `ordinal` is the suffix of /grades/<n>, counted from 1 in file order; anything
// that is not a recorded attempt answers 404. The filters are only carried on
// the back link, so leaving an attempt returns to the view it was opened from.
net::Response attempt_page(const GradeLog& log, std::string_view ordinal,
                           std::string_view lesson = {}, std::string_view student = {},
                           std::string_view result = {}, std::string_view term = {});

}  // namespace sc::app
