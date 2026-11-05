#include "lesson/grader.hpp"

#include <cstdio>

namespace sc::lesson {
namespace {

std::string format_number(f64 value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}

bool evaluate_check(const RubricCheck& check, const sim::Plant& plant, bool program_healthy,
                    std::string& detail) {
    if (check.kind == CheckKind::ProgramHealthy) {
        detail = program_healthy ? "program healthy" : "program faulted";
        return program_healthy;
    }
    if (check.device >= plant.count()) {
        detail = "lesson references a device that the plant does not have";
        return false;
    }

    const sim::Device& device = plant.device(check.device);
    switch (check.kind) {
        case CheckKind::DeviceOutputTrue:
            detail = "output=" + std::string(device.output ? "true" : "false");
            return device.output;
        case CheckKind::DeviceOutputFalse:
            detail = "output=" + std::string(device.output ? "true" : "false");
            return !device.output;
        case CheckKind::BoxCountAtLeast:
            detail = "boxes=" + std::to_string(device.box_count) + ", required>=" +
                     format_number(check.threshold);
            return static_cast<f64>(device.box_count) >= check.threshold;
        case CheckKind::SpeedAtLeast:
            detail = "speed=" + format_number(device.speed) + ", required>=" +
                     format_number(check.threshold);
            return device.speed >= check.threshold;
        case CheckKind::ProcessedCountAtLeast:
            detail = "processed=" + std::to_string(device.done_count) + ", required>=" +
                     format_number(check.threshold);
            return static_cast<f64>(device.done_count) >= check.threshold;
        case CheckKind::ProgramHealthy:
            break;
    }
    detail = "unknown check kind";
    return false;
}

}  // namespace

GradeReport evaluate(const Lesson& lesson, const sim::Plant& plant, bool program_healthy) {
    GradeReport report;
    report.lesson_id = lesson.id;

    for (const RubricCheck& check : lesson.rubric) {
        CheckResult result;
        result.id = check.id;
        result.description = check.description;
        result.passed = evaluate_check(check, plant, program_healthy, result.detail);
        if (result.passed) {
            ++report.passed_count;
        }
        report.checks.push_back(std::move(result));
    }
    return report;
}

GradeReport refused_report(const Lesson& lesson, std::string_view feature) {
    GradeReport report;
    report.lesson_id = lesson.id;

    CheckResult toolbox;
    toolbox.id = "toolbox";
    toolbox.description = "Uses only the instructions this lesson grants";
    toolbox.passed = false;
    toolbox.detail = "uses '" + std::string(feature) + "', which this lesson does not grant";
    report.checks.push_back(std::move(toolbox));

    for (const RubricCheck& check : lesson.rubric) {
        CheckResult skipped;
        skipped.id = check.id;
        skipped.description = check.description;
        skipped.passed = false;
        skipped.detail = "not run: the program was refused";
        report.checks.push_back(std::move(skipped));
    }

    report.passed_count = 0;
    return report;
}

}  // namespace sc::lesson
