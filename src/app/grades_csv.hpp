#pragma once

#include "app/grade_log.hpp"
#include "net/portal_server.hpp"

namespace sc::app {

// GET /grades.csv: one row per attempt in file order, for a gradebook. The
// submitted program is left out on purpose: a teacher marking a class wants the
// score and the missed checks, not a term of PLC source in a spreadsheet cell.
// The row index is the same ordinal /grades/<n> uses, so a row can be traced
// back to its attempt page.
net::Response grades_csv(const GradeLog& log);

}  // namespace sc::app
