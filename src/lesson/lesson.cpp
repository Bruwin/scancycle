#include "lesson/lesson.hpp"

namespace sc::lesson {

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

    lesson.rubric.push_back(RubricCheck{"drive-energised", "Motor output is on",
                                        CheckKind::DeviceOutputTrue, 0, 0.0});
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
