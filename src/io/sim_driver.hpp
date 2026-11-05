#pragma once
// Software PLC driver: connects a compiled Structured Text program to a Plant
// exactly the way a physical driver would. This class is the reference
// implementation of the scan contract that every hardware driver must match.
#include "io/driver.hpp"
#include "io/force_table.hpp"
#include "io/tag_database.hpp"
#include "plc/vm.hpp"
#include "sim/plant.hpp"

#include <string>

namespace sc::io {

class SimDriver final : public PlcDriver {
public:
    SimDriver(sim::Plant& plant, const TagDatabase& tags, plc::Vm& vm) noexcept
        : plant_(plant), tags_(tags), vm_(vm) {}

    bool connect() override;
    void disconnect() override;
    bool connected() const override { return connected_; }

    bool read_inputs() override;
    bool write_outputs() override;

    const char* name() const override { return "sim"; }
    std::string last_error() const override { return error_; }

    // Latch inputs, execute one scan, flush outputs. Returns false and records a
    // diagnostic if the program faulted.
    bool run_scan(i64 now_ms);

    i64 scans() const { return scans_; }

    // Optional. Forces are applied where a real PLC applies them: after an input
    // is latched, and before an output is flushed.
    void set_forces(const ForceTable* forces) { forces_ = forces; }

private:
    sim::Plant& plant_;
    const TagDatabase& tags_;
    plc::Vm& vm_;
    bool connected_ = false;
    i64 scans_ = 0;
    const ForceTable* forces_ = nullptr;
    std::string error_;
};

}  // namespace sc::io
