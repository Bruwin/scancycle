#pragma once
// Runs a lesson's reference program against the lesson's own cell and grades the
// result. This is how a lesson proves itself: an exercise is only valid if its
// reference solution passes its own rubric on the deterministic plant, which is
// the regression test the teacher portal depends on.
#include "core/types.hpp"
#include "lesson/grader.hpp"
#include "lesson/lesson.hpp"

#include <string>

namespace sc {

struct LessonRunConfig {
    f64 seconds = 30.0;
    f64 tick_seconds = 1.0 / 60.0;
    i64 plc_scan_ms = 10;

    // Grades this source instead of the lesson's reference solution. Empty means
    // the reference, which is what the lesson gate runs; the source is compiled
    // in the lesson's reference language.
    std::string program;
};

struct LessonRunResult {
    bool ok = false;   // the run itself completed (not "the student passed")

    // The toolbox refused the program, so nothing ran. report still holds a
    // grade: the refusal plus every rubric check marked not run.
    bool refused = false;
    bool faulted = false;
    std::string error;
    lesson::GradeReport report;
    i64 scans = 0;
    u32 boxes_processed = 0;
};

LessonRunResult run_lesson(const lesson::Lesson& lesson, LessonRunConfig config = {});

}  // namespace sc
