#pragma once
// The printable class log: every recorded attempt, newest first. Kept out of
// sc_net because the on-disk format is an app concern, not a transport one.
#include "app/grade_log.hpp"
#include "net/portal_server.hpp"

#include <string_view>

namespace sc::app {

net::Response grades_page(const GradeLog& log);

// One recorded attempt with its full report, for the page a teacher prints.
// `ordinal` is the suffix of /grades/<n>, counted from 1 in file order; anything
// that is not a recorded attempt answers 404.
net::Response attempt_page(const GradeLog& log, std::string_view ordinal);

}  // namespace sc::app
