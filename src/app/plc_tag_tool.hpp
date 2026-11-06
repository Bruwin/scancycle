#pragma once
// One-shot libplctag reads and writes for bring-up against a real controller.
// The scan driver is shaped for a VM and a tag map; commissioning one tag needs
// a way to look at one boolean before trusting either. This tool uses the same
// PlcTagApi seam as the driver, but a write is still allowlisted and
// session-gated through CommissioningPolicy: typing --write is the operator's
// opt-in, not a bypass.
#include "core/types.hpp"
#include "io/commissioning.hpp"
#include "io/plc_tag_driver.hpp"

#include <string>

namespace sc::app {

struct PlcTagToolConfig {
    std::string tag;  // A complete libplctag attribute string.
    int timeout_ms = 1000;

    // A null policy gets a bounded one-shot session with this exact tag
    // allowlisted. Tests inject a policy here to prove that denial is enforced.
    io::CommissioningPolicy* commissioning = nullptr;
};

struct PlcTagToolResult {
    bool ok = false;
    bool value = false;
    std::string error;
};

PlcTagToolResult plc_tag_read(const PlcTagToolConfig& config, io::PlcTagApi& api);

// Reads the current value before setting bit 0, so a write to a word tag keeps
// the other bits intact.
PlcTagToolResult plc_tag_write(const PlcTagToolConfig& config, io::PlcTagApi& api,
                               bool value);

}  // namespace sc::app
