#pragma once
// Composition root. Owns the plant, the tag database, the compiled program, the
// VM and the driver, and advances them in the correct order: plant first, then
// the scan on its own period. Nothing else in the codebase is allowed to know
// how these pieces fit together.
#include "core/types.hpp"
#include "io/force_table.hpp"
#include "io/sim_driver.hpp"
#include "lesson/grader.hpp"
#include "plc/vm.hpp"
#include "sim/plant.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sc {

// The composition root runs one program. Ladder rung files are lowered through
// the same netlist pass the CLI uses, so a lesson can ship either language.
enum class ProgramLanguage : u8 { StructuredText = 0, Ladder };

struct DeviceSpec {
    sim::DeviceKind kind = sim::DeviceKind::Motor;
    std::string name;
};

struct AppConfig {
    std::string program_source;
    ProgramLanguage program_language = ProgramLanguage::StructuredText;
    std::vector<DeviceSpec> devices;
    u64 world_seed = 1;
    f64 tick_seconds = 1.0 / 60.0;
    i64 plc_scan_ms = 10;

    // The lesson's toolbox. Empty means every instruction is available; otherwise
    // a program that uses anything else is refused before it can be graded.
    std::vector<std::string> allowed_instructions;
};

class Application {
public:
    explicit Application(AppConfig config);

    // A prepared cell. Lessons ship the plant they are taught on, wiring
    // included, so the root must accept one instead of building it from specs.
    Application(AppConfig config, sim::Plant plant);

    ~Application();

    bool initialize();

    // Advances the plant and runs the PLC on its own period. Returns false if
    // the program faulted; the diagnostic is then in error().
    bool run_headless(i32 ticks);

    // Executes exactly one scan at the next scan instant. Time advances by one
    // scan period so timers progress, but the plant does not move: a step is for
    // watching the logic, and moving the machine would make each step depend on
    // how many steps came before it.
    bool step_scan();

    // Pins a bound tag, the way an engineer forces I/O at the rack. Refused when
    // the name is not a tag this program is bound to, so a typo cannot create a
    // force that silently does nothing.
    bool set_force(std::string name, bool value);
    bool clear_force(std::string_view name);
    void clear_forces();
    const io::ForceTable& forces() const { return forces_; }

    struct Watch {
        std::string name;
        plc::Value value;
        bool present = false;  // false when the program has no such variable
        bool forced = false;
    };

    // Reads the named variables out of the program image without changing it.
    // Unknown names come back absent rather than blank, so an editor can show a
    // typo instead of quietly dropping it.
    std::vector<Watch> watch(const std::vector<std::string>& names) const;

    // A complete picture of the monitor at one instant: the tag values, what is
    // pinned, and which scan produced them. Pure and cheap, so an editor or a
    // debugger can poll it as often as it likes without changing what it sees.
    struct WatchSnapshot {
        i64 scan = 0;
        i64 now_ms = 0;
        bool faulted = false;
        std::vector<Watch> tags;
        std::vector<io::Force> forces;
    };

    WatchSnapshot snapshot(const std::vector<std::string>& names) const;

    const sim::Plant& plant() const { return plant_; }
    plc::Vm* plc() { return vm_.get(); }
    const std::string& error() const { return error_; }
    bool faulted() const { return faulted_; }
    i64 scans() const;

    // Devices the program never mentions. A misspelt tag shows up here instead
    // of silently doing nothing, which is the classic PLC mistake.
    const std::vector<std::string>& unbound_devices() const { return unbound_devices_; }

    lesson::GradeReport grade(const lesson::Lesson& lesson) const;

private:
    AppConfig config_;
    sim::Plant plant_;
    io::TagDatabase tags_;
    io::ForceTable forces_;  // declared before the driver, which points at it
    plc::CompiledProgram program_;
    std::unique_ptr<plc::Vm> vm_;
    std::unique_ptr<io::SimDriver> driver_;
    std::vector<std::string> unbound_devices_;
    i64 now_ms_ = 0;
    i64 scan_accumulator_ms_ = 0;
    bool faulted_ = false;
    std::string error_;
};

}  // namespace sc
