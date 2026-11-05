#include "app/application.hpp"

#include "plc/compiler.hpp"
#include "plc/ladder_text.hpp"
#include "plc/netlist.hpp"
#include "plc/parser.hpp"

#include <utility>

namespace sc {

Application::Application(AppConfig config) : config_(std::move(config)) {}

Application::Application(AppConfig config, sim::Plant plant)
    : config_(std::move(config)), plant_(std::move(plant)) {}

Application::~Application() = default;

bool Application::initialize() {
    refused_feature_.clear();
    if (config_.plc_scan_ms <= 0) {
        error_ = "plc_scan_ms must be positive";
        return false;
    }

    for (const DeviceSpec& spec : config_.devices) {
        plant_.add(spec.kind, spec.name);
    }

    if (config_.program_language == ProgramLanguage::Ladder) {
        plc::LadderParseResult parsed = plc::parse_ladder_source(config_.program_source);
        if (!parsed.ok) {
            error_ = parsed.diagnostics.empty() ? "failed to parse the ladder program"
                                                : parsed.diagnostics.front().message;
            return false;
        }
        plc::NetlistResult compiled =
            plc::compile_netlist("ladder", parsed.source.rungs, parsed.source.vars);
        if (!compiled.ok) {
            error_ = compiled.diagnostics.empty() ? "failed to compile the ladder program"
                                                 : compiled.diagnostics.front().message;
            return false;
        }
        program_ = std::move(compiled.program);
    } else {
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
    }

    // A lesson that grants a toolbox means it. Checking the compiled program
    // covers both source languages, and it holds the lesson's own reference
    // solution to the same list, so an exercise cannot ship an answer it forbids.
    for (const std::string& feature : plc::features_used(program_)) {
        if (!lesson::toolbox_allows(config_.allowed_instructions, feature)) {
            refused_feature_ = feature;
            error_ = "this lesson does not grant '" + feature + "'";
            return false;
        }
    }

    // Bind by what the program declares, not by what the plant contains. A
    // lesson cell can hold devices a given exercise never touches, and the
    // driver refuses to latch an input whose variable is absent.
    unbound_devices_.clear();
    for (u32 index = 0; index < plant_.count(); ++index) {
        const std::string& name = plant_.device(index).name;
        const bool drives = program_.find_var(name) >= 0;
        const bool senses = program_.find_var(name + "_fb") >= 0;

        if (drives) {
            tags_.bind(name, io::TagDirection::Output, index);
        }
        if (senses) {
            tags_.bind(name + "_fb", io::TagDirection::Input, index);
        }
        if (!drives && !senses) {
            unbound_devices_.push_back(name);
        }
    }

    vm_ = std::make_unique<plc::Vm>(program_);
    if (config_.driver_factory) {
        driver_ = config_.driver_factory(*vm_);
        if (!driver_) {
            error_ = "the configured driver could not be created";
            return false;
        }
        sim_driver_ = nullptr;
    } else {
        auto sim = std::make_unique<io::SimDriver>(plant_, tags_, *vm_);
        sim->set_forces(&forces_);
        sim_driver_ = sim.get();
        driver_ = std::move(sim);
    }
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

bool Application::step_scan() {
    if (!driver_) {
        error_ = "application is not initialised";
        return false;
    }
    if (faulted_) {
        error_ = "the program has faulted";
        return false;
    }

    now_ms_ += config_.plc_scan_ms;
    if (!driver_->run_scan(now_ms_)) {
        faulted_ = true;
        error_ = driver_->last_error();
        return false;
    }
    return true;
}

bool Application::set_force(std::string name, bool value) {
    if (!driver_) {
        error_ = "application is not initialised";
        return false;
    }
    if (sim_driver_ == nullptr) {
        error_ = "forces are only available on the simulated driver";
        return false;
    }
    if (tags_.find(name) < 0) {
        error_ = "no tag named '" + name + "' is bound to this program";
        return false;
    }
    forces_.set(std::move(name), value);
    return true;
}

bool Application::clear_force(std::string_view name) {
    if (!forces_.clear(name)) {
        error_ = "nothing is forced under '" + std::string(name) + "'";
        return false;
    }
    return true;
}

void Application::clear_forces() { forces_.clear_all(); }

std::vector<Application::Watch> Application::watch(
    const std::vector<std::string>& names) const {
    std::vector<Watch> out;
    out.reserve(names.size());
    for (const std::string& name : names) {
        Watch entry;
        entry.name = name;
        if (vm_) {
            entry.value = vm_->get(name);
            entry.present = entry.value.type != plc::ValueType::Void;
        }
        entry.forceable = tags_.find(name) >= 0;
        entry.forced = forces_.find(name, entry.force_value);
        out.push_back(std::move(entry));
    }
    return out;
}

Application::WatchSnapshot Application::snapshot(const std::vector<std::string>& names) const {
    WatchSnapshot out;
    out.scan = scans();
    out.now_ms = now_ms_;
    out.faulted = faulted_;
    out.tags = watch(names);
    out.forces = forces_.entries();
    return out;
}

i64 Application::scans() const { return driver_ ? driver_->scans() : 0; }

lesson::GradeReport Application::grade(const lesson::Lesson& lesson) const {
    return lesson::evaluate(lesson, plant_, !faulted_);
}

}  // namespace sc
