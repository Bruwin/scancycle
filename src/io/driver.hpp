#pragma once
// The contract every PLC transport implements, simulated or physical. A driver
// owns a double-buffered process image: read_inputs() latches the plant into the
// PLC image, write_outputs() flushes the PLC image back out. Nothing else may
// touch the plant between those two calls.
#include "core/types.hpp"

#include <string>

namespace sc::io {

class PlcDriver {
public:
    virtual ~PlcDriver() = default;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool connected() const = 0;

    virtual bool read_inputs() = 0;
    virtual bool write_outputs() = 0;

    // One complete scan: latch inputs, run the program, flush outputs. The
    // composition root drives every transport through this call, so a live
    // controller and the simulated cell cannot drift apart.
    virtual bool run_scan(i64 now_ms) = 0;
    virtual i64 scans() const = 0;

    virtual const char* name() const = 0;
    virtual std::string last_error() const = 0;
};

}  // namespace sc::io
