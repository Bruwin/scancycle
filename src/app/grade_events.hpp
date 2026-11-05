#pragma once
// The small JSON messages the portal's live feed publishes. They deliberately
// carry the attempt's verdict, not its program: a teacher watching the log
// needs to know that something changed, not to receive a term of source over a
// socket.
#include "app/grade_log.hpp"

#include <string>
#include <string_view>

namespace sc::app {

std::string grade_recorded_json(const GradeRecord& record);
std::string term_changed_json(std::string_view term);

}  // namespace sc::app
