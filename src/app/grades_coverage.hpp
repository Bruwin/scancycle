#pragma once
// A student-by-lesson view of the filtered log. The lesson summary answers "how
// is this lesson going" and the student summary answers "how is this student
// doing"; this one answers "who has not tried this lesson", which neither of the
// totals can show.
#include "app/grade_log.hpp"

#include <span>
#include <string>
#include <vector>

namespace sc::app {

// Empty when no attempt names a student: coverage is about people a teacher can
// follow up with, and unnamed attempts still count in the student summary. Each
// cell is the passed count over the attempt count, and links to that pair's
// filtered log so a gap can be investigated in one click.
//
// `catalog` is every lesson the portal knows about, so a lesson nobody has
// attempted still gets a column of dashes rather than disappearing. A lesson the
// log remembers but the catalog has dropped keeps its column too: an old term's
// report should not lose the work it recorded.
std::string student_coverage(const std::vector<const GradeRecord*>& records,
                             std::span<const std::string> catalog);

}  // namespace sc::app
