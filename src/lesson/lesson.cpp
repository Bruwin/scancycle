#include "lesson/lesson.hpp"

#include <utility>

namespace sc::lesson {
namespace {

// Device indices are looked up by name rather than hard-coded: the lesson owns
// its cell, so a wiring change in the plant must not silently retarget a rubric.
RubricCheck check_on(const sim::Plant& plant, std::string name, std::string id,
                     std::string description, CheckKind kind, f64 threshold = 0.0) {
    RubricCheck check;
    check.id = std::move(id);
    check.description = std::move(description);
    check.kind = kind;
    check.device = plant.find(name);
    check.threshold = threshold;
    return check;
}

}  // namespace

Lesson make_conveyor_interlock_lesson() {
    Lesson lesson;
    lesson.id = "conveyor-interlock";
    lesson.title = "Conveyor interlock";
    lesson.objective =
        "Latch a conveyor on with a momentary start, drop it out on stop or jam, "
        "and hold the drive off for two seconds after the latch seals.";
    lesson.world_seed = 7;

    // A deliberate restriction: this lesson is about boolean logic and a single
    // TON. Anything beyond that is next week's material.
    lesson.allowed_instructions = {"AND", "OR", "NOT", "TON", "BOOL", ":="};

    lesson.starting_plant.add(sim::DeviceKind::Motor, "motor_run");

    // The student is handed a panel with start already pressed, so the
    // reference solution can be graded without a human in the loop.
    lesson.preset_inputs.push_back({"start_btn", true});

    lesson.rubric.push_back(check_on(lesson.starting_plant, "motor_run",
                                     "drive-energised", "Motor output is on",
                                     CheckKind::DeviceOutputTrue));
    lesson.rubric.push_back(
        RubricCheck{"program-healthy", "Program runs without a fault",
                    CheckKind::ProgramHealthy, 0, 0.0});

    lesson.reference_solution = R"st(PROGRAM ConveyorInterlock
VAR
    start_btn   : BOOL;
    stop_btn    : BOOL;
    motor_run   : BOOL;
    motor_fb    : BOOL;
    run_latch   : BOOL;
    delay_timer : TON;
END_VAR
    run_latch := (run_latch OR start_btn) AND NOT stop_btn;
    delay_timer(IN := run_latch, PT := T#2s);
    motor_run := delay_timer.Q;
END_PROGRAM
)st";
    return lesson;
}

Lesson make_press_cycle_lesson() {
    Lesson lesson;
    lesson.id = "press-cycle";
    lesson.title = "Press cell: bring the line up";
    lesson.objective =
        "Drive the infeed belt, enable the press and light the stack light, then "
        "watch three parts travel the whole cell: belt, press, outfeed.";
    lesson.world_seed = 11;

    // The student programs the cell they can see. The plant ships wired, so the
    // exercise is the program, not the plumbing.
    lesson.starting_plant = sim::make_vertical_slice_plant();

    lesson.allowed_instructions = {"BOOL", ":=", "AND", "OR", "NOT", "TON"};

    lesson.rubric.push_back(check_on(lesson.starting_plant, "press", "press-processed",
                                     "Press completes three parts",
                                     CheckKind::ProcessedCountAtLeast, 3.0));
    lesson.rubric.push_back(check_on(lesson.starting_plant, "infeed_belt", "infeed-running",
                                     "Infeed belt is driven", CheckKind::DeviceOutputTrue));
    lesson.rubric.push_back(check_on(lesson.starting_plant, "run_light", "run-light-on",
                                     "Run light is on", CheckKind::DeviceOutputTrue));
    lesson.rubric.push_back(RubricCheck{"program-healthy", "Program runs without a fault",
                                        CheckKind::ProgramHealthy, 0, 0.0});

    // A one-second delayed start, so the program shows a timer doing work rather
    // than four assignments of the same literal.
    lesson.reference_solution = R"st(PROGRAM PressCell
VAR
    cell_run     : BOOL;
    infeed_belt  : BOOL;
    press        : BOOL;
    outfeed_belt : BOOL;
    run_light    : BOOL;
    start_delay  : TON;
END_VAR
    cell_run := TRUE;
    start_delay(IN := cell_run, PT := T#1s);

    infeed_belt := start_delay.Q;
    press := start_delay.Q;
    outfeed_belt := start_delay.Q;
    run_light := start_delay.Q;
END_PROGRAM
)st";
    return lesson;
}

bool instruction_allowed(const Lesson& lesson, std::string_view instruction) {
    if (lesson.allowed_instructions.empty()) {
        return true;
    }
    for (const std::string& allowed : lesson.allowed_instructions) {
        if (allowed == instruction) {
            return true;
        }
    }
    return false;
}

}  // namespace sc::lesson
