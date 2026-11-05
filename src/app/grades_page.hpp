#pragma once
// The printable class log: every recorded attempt, newest first. Kept out of
// sc_net because the on-disk format is an app concern, not a transport one.
#include "app/grade_log.hpp"
#include "net/portal_server.hpp"

namespace sc::app {

net::Response grades_page(const GradeLog& log);

}  // namespace sc::app
