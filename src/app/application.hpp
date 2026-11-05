#pragma once
// Composition root. Owns the plant, the tag database, the compiled program, the
// VM and the driver, and advances them in the correct order: plant first, then
// the scan on its own period. Nothing else in the codebase is allowed to know
// how these pieces fit together.
#include "core/types.hpp"
#include "io/sim_driver.hpp"
#include "lesson/grader.hpp"
#include "plc/vm.hpp"
#include "sim/plant.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sc {

struct DeviceSpec {
    sim::DeviceKind kind = sim::DeviceKind::Motor;
    std::string name;
};

struct AppConfig {
    std::string program_source;
    std::vector<DeviceSpec> devices;
    u64 world_seed = 1;
    f64 tick_seconds = 1.0 / 60.0;
    i64 plc_scan_ms = 10;
};

class Application {
public:
    explicit Application(AppConfig config);
    ~Application();

    bool initialize();

    // Advances the plant and runs the PLC on its own period. Returns false if
    // the program faulted; the diagnostic is then in error().
    bool run_headless(i32 ticks);

    const sim::Plant& plant() const { return plant_; }
    plc::Vm* plc() { return vm_.get(); }
    const std::string& error() const { return error_; }
    bool faulted() const { return faulted_; }

    lesson::GradeReport grade(const lesson::Lesson& lesson) const;

private:
    AppConfig config_;
    sim::Plant plant_;
    io::TagDatabase tags_;
    plc::CompiledProgram program_;
    std::unique_ptr<plc::Vm> vm_;
    std::unique_ptr<io::SimDriver> driver_;
    i64 now_ms_ = 0;
    i64 scan_accumulator_ms_ = 0;
    bool faulted_ = false;
    std::string error_;
};

}  // namespace sc
