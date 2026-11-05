#pragma once
// The portal's grade routes. Both run the same submission; `/api/grade` answers
// with JSON for tooling and `/grade` answers with a page for the catalog form.
// Kept out of the server binary so a test can call the handlers directly, and
// out of sc_net so the transport stays unaware of what a lesson run is.
//
// The request is a form body: `lesson=<id>` and an optional `program=<source>`
// percent-encoded. An empty program grades the lesson's own reference solution,
// which is how a teacher checks that a lesson is still healthy.
#include "lesson/lesson.hpp"
#include "net/portal_server.hpp"

#include <vector>

namespace sc::app {

net::Response grade_lesson(const std::vector<lesson::Lesson>& lessons,
                           const net::Request& request);

net::Response grade_lesson_page(const std::vector<lesson::Lesson>& lessons,
                                const net::Request& request);

}  // namespace sc::app
