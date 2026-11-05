#pragma once

#include "app/grade_log.hpp"

#include <string>
#include <vector>

namespace sc::app {

// A table of every named student's attempts, passes, pass rate and latest
// attempt, for the class log. Returns an empty string when no attempt carries a
// name: an all-anonymous log has no students to compare, and one `(unnamed)` row
// above the attempt list would be noise rather than a summary.
std::string student_summary(const std::vector<const GradeRecord*>& records);

}  // namespace sc::app
