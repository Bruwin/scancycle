#pragma once
// A lesson is data. It never compiles to code: it carries the seed, the plant,
// the allowed instruction set and the rubric, so a teacher can author a lesson
// in a text editor and version it alongside the course.
#include "core/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sc::lesson {

enum class CheckKind : u8 {
    DeviceOutputTrue = 0,
    DeviceOutputFalse,
    BoxCountAtLeast,
    SpeedAtLeast,
    ProgramHealthy,
};

struct RubricCheck {
    std::string id;
    std::string description;
    CheckKind kind = CheckKind::ProgramHealthy;
    u32 device = 0;
    f64 threshold = 0.0;
};

struct Lesson {
    std::string id;
    std::string title;
    std::string objective;
    u64 world_seed = 1;

    // Empty means "every instruction is available". Otherwise this is the
    // week's toolbox: students may only use what the lesson grants.
    std::vector<std::string> allowed_instructions;

    std::vector<RubricCheck> rubric;

    // Runs in CI as a regression test of the lesson itself.
    std::string reference_solution;
};

Lesson make_conveyor_interlock_lesson();

bool instruction_allowed(const Lesson& lesson, std::string_view instruction);

}  // namespace sc::lesson
