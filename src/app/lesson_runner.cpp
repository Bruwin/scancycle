#include "app/lesson_runner.hpp"

#include "app/application.hpp"

#include <utility>

namespace sc {

LessonRunResult run_lesson(const lesson::Lesson& lesson, LessonRunConfig config) {
    LessonRunResult result;

    if (config.tick_seconds <= 0.0 || config.seconds <= 0.0 || config.plc_scan_ms <= 0) {
        result.error = "a lesson run needs a positive duration, tick and scan period";
        return result;
    }
    if (lesson.reference_solution.empty()) {
        result.error = "lesson '" + lesson.id + "' has no reference solution";
        return result;
    }

    AppConfig app_config;
    app_config.program_source = lesson.reference_solution;
    app_config.program_language =
        lesson.reference_language == lesson::ReferenceLanguage::Ladder
            ? ProgramLanguage::Ladder
            : ProgramLanguage::StructuredText;
    app_config.tick_seconds = config.tick_seconds;
    app_config.plc_scan_ms = config.plc_scan_ms;

    Application app(std::move(app_config), lesson.starting_plant);
    if (!app.initialize()) {
        result.error = app.error();
        return result;
    }

    for (const std::pair<std::string, bool>& preset : lesson.preset_inputs) {
        if (!app.plc()->set(preset.first, plc::Value::boolean(preset.second))) {
            result.error = "lesson presses a button the program never declares: " +
                           preset.first;
            return result;
        }
    }

    const i32 ticks = static_cast<i32>(config.seconds / config.tick_seconds);
    result.ok = app.run_headless(ticks);
    result.scans = app.scans();
    result.faulted = app.faulted();
    if (!result.ok) {
        result.error = app.error();
    }

    for (u32 index = 0; index < app.plant().count(); ++index) {
        const sim::Device& device = app.plant().device(index);
        if (device.kind == sim::DeviceKind::Machine) {
            result.boxes_processed += static_cast<u32>(device.done_count);
        }
    }

    result.report = app.grade(lesson);
    return result;
}

}  // namespace sc
