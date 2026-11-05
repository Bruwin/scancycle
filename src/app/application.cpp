#include "app/application.hpp"

#include "plc/compiler.hpp"
#include "plc/parser.hpp"

#include <utility>

namespace sc {

Application::Application(AppConfig config) : config_(std::move(config)) {}

Application::~Application() = default;

bool Application::initialize() {
    if (config_.plc_scan_ms <= 0) {
        error_ = "plc_scan_ms must be positive";
        return false;
    }

    for (const DeviceSpec& spec : config_.devices) {
        const u32 index = plant_.add(spec.kind, spec.name);
        tags_.bind(spec.name, io::TagDirection::Output, index);
        tags_.bind(spec.name + "_fb", io::TagDirection::Input, index);
    }

    plc::ParseResult parsed = plc::parse_program(config_.program_source);
    if (!parsed.program) {
        error_ = parsed.diagnostics.empty() ? "failed to parse the program"
                                            : parsed.diagnostics.front().message;
        return false;
    }

    plc::CompileResult compiled = plc::compile(*parsed.program);
    if (!compiled.ok) {
        error_ = compiled.diagnostics.empty() ? "failed to compile the program"
                                             : compiled.diagnostics.front().message;
        return false;
    }

    program_ = std::move(compiled.program);
    vm_ = std::make_unique<plc::Vm>(program_);
    driver_ = std::make_unique<io::SimDriver>(plant_, tags_, *vm_);
    if (!driver_->connect()) {
        error_ = driver_->last_error();
        return false;
    }
    return true;
}

bool Application::run_headless(i32 ticks) {
    if (!driver_) {
        error_ = "application is not initialised";
        return false;
    }

    const i64 step_ms = static_cast<i64>(config_.tick_seconds * 1000.0);
    if (step_ms <= 0) {
        error_ = "tick_seconds is too small to advance the simulation";
        return false;
    }

    for (i32 tick = 0; tick < ticks; ++tick) {
        plant_.tick(config_.tick_seconds);
        now_ms_ += step_ms;
        scan_accumulator_ms_ += step_ms;

        while (scan_accumulator_ms_ >= config_.plc_scan_ms) {
            if (!driver_->run_scan(now_ms_)) {
                faulted_ = true;
                error_ = driver_->last_error();
                return false;
            }
            scan_accumulator_ms_ -= config_.plc_scan_ms;
        }
    }
    return true;
}

lesson::GradeReport Application::grade(const lesson::Lesson& lesson) const {
    return lesson::evaluate(lesson, plant_, !faulted_);
}

}  // namespace sc
