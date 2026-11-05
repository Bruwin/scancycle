#pragma once
// The printable class log: every recorded attempt, newest first. Kept out of
// sc_net because the on-disk format is an app concern, not a transport one.
#include "app/grade_log.hpp"
#include "net/portal_server.hpp"

#include <string_view>

namespace sc::app {

// `lesson` and `student` are the query fields of the same names. Empty means
// unfiltered; both empty shows the whole log, and both given narrows to that
// student's attempts at that lesson. A value nobody attempted is an empty page,
// not a 404: the log itself exists and the query is well formed.
net::Response grades_page(const GradeLog& log, std::string_view lesson = {},
                          std::string_view student = {});

// One recorded attempt with its full report, for the page a teacher prints.
// `ordinal` is the suffix of /grades/<n>, counted from 1 in file order; anything
// that is not a recorded attempt answers 404. The filters are only carried on
// the back link, so leaving an attempt returns to the view it was opened from.
net::Response attempt_page(const GradeLog& log, std::string_view ordinal,
                           std::string_view lesson = {}, std::string_view student = {});

}  // namespace sc::app
