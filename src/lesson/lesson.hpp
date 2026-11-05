#pragma once
// A lesson is data. It never compiles to code: it carries the seed, the plant,
// the allowed instruction set and the rubric, so a teacher can author a lesson
// in a text editor and version it alongside the course.
#include "core/types.hpp"
#include "sim/plant.hpp"

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sc::lesson {

enum class CheckKind : u8 {
    DeviceOutputTrue = 0,
    DeviceOutputFalse,
    BoxCountAtLeast,
    SpeedAtLeast,
    ProgramHealthy,
    // Parts a machine has finished. Throughput, not activity: a press that is
    // merely enabled has processed nothing.
    ProcessedCountAtLeast,
};

struct RubricCheck {
    std::string id;
    std::string description;
    CheckKind kind = CheckKind::ProgramHealthy;
    u32 device = 0;
    f64 threshold = 0.0;
};

// A reference program is written in one of the two text languages the runtime
// compiles. Ladder lessons are rung files, which is what a student draws.
enum class ReferenceLanguage : u8 { StructuredText = 0, Ladder };

struct Lesson {
    std::string id;
    std::string title;
    std::string objective;
    u64 world_seed = 1;

    // The cell the lesson is taught on, wiring included. A lesson that leaves
    // this empty is graded against whatever plant the caller supplies.
    sim::Plant starting_plant;

    // Operator-panel state applied once, before the first scan: the buttons a
    // student would already be holding at t=0. A latch that is never pressed
    // cannot be graded.
    std::vector<std::pair<std::string, bool>> preset_inputs;

    // Empty means "every instruction is available". Otherwise this is the
    // week's toolbox: students may only use what the lesson grants.
    std::vector<std::string> allowed_instructions;

    std::vector<RubricCheck> rubric;

    ReferenceLanguage reference_language = ReferenceLanguage::StructuredText;

    // Runs in CI as a regression test of the lesson itself.
    std::string reference_solution;
};

Lesson make_conveyor_interlock_lesson();

// The M3 cell: bring the belt, press and outfeed up and get parts through.
Lesson make_press_cycle_lesson();

// The same cell, but the line counts its parts and stops feeding when the
// batch is full: a delayed start plus a counter with a visible CV.
Lesson make_batch_cycle_lesson();

// The batch lesson written as ladder rungs instead of Structured Text, so the
// same cell is available in the language a student draws.
Lesson make_ladder_batch_lesson();

// Every lesson the shipped builds offer. The CLI, the portal server and the
// lesson gate all read this one list, so a lesson cannot exist in one surface
// and be missing from another.
std::vector<Lesson> make_builtin_lessons();

bool instruction_allowed(const Lesson& lesson, std::string_view instruction);

// The same rule for a bare list, so the runtime can enforce a toolbox without
// inventing a Lesson. An empty list grants everything.
bool toolbox_allows(std::span<const std::string> allowed, std::string_view instruction);

}  // namespace sc::lesson
