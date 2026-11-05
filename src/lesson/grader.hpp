#pragma once
// Grades a student's running plant against the lesson rubric. Every check is an
// assertion over the deterministic simulation, which is why the same submission
// grades identically on the teacher's machine and the student's.
#include "lesson/lesson.hpp"
#include "sim/plant.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sc::lesson {

struct CheckResult {
    std::string id;
    std::string description;
    bool passed = false;
    std::string detail;
};

struct GradeReport {
    std::string lesson_id;
    std::vector<CheckResult> checks;
    std::size_t passed_count = 0;

    bool passed() const { return passed_count == checks.size(); }
};

GradeReport evaluate(const Lesson& lesson, const sim::Plant& plant, bool program_healthy);

// Serialises a report for the portal. Next to the report type so the shape of a
// grade and the shape of its JSON cannot drift apart.
std::string grade_report_json(const GradeReport& report);

// The report for a submission the toolbox turned away before it could run: one
// failing row naming the withheld instruction, then every rubric check marked
// not run. A refusal is an answer about the student's program, so it belongs in
// the report rather than in the transport's error channel.
GradeReport refused_report(const Lesson& lesson, std::string_view feature);

}  // namespace sc::lesson
