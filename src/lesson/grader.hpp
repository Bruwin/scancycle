#pragma once
// Grades a student's running plant against the lesson rubric. Every check is an
// assertion over the deterministic simulation, which is why the same submission
// grades identically on the teacher's machine and the student's.
#include "lesson/lesson.hpp"
#include "sim/plant.hpp"

#include <string>
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

}  // namespace sc::lesson
