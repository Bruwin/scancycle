#include "io/sim_driver.hpp"

namespace sc::io {

bool SimDriver::connect() {
    connected_ = true;
    error_.clear();
    return true;
}

void SimDriver::disconnect() { connected_ = false; }

bool SimDriver::read_inputs() {
    if (!connected_) {
        error_ = "driver is not connected";
        return false;
    }

    for (u32 index = 0; index < tags_.size(); ++index) {
        const Tag& tag = tags_.tag(index);
        if (tag.direction != TagDirection::Input) {
            continue;
        }
        bool value = false;
        if (!tags_.read_bool(index, plant_, value)) {
            error_ = "failed to read input tag '" + tag.name + "'";
            return false;
        }
        if (!vm_.set(tag.name, plc::Value::boolean(value))) {
            error_ = "PLC program has no variable named '" + tag.name + "'";
            return false;
        }

        // The field value was read and then discarded on purpose. Forcing an
        // input means the program sees the forced value while the device keeps
        // reporting its own, which is what makes a force visible as a force.
        bool forced = false;
        if (forces_ != nullptr && forces_->find(tag.name, forced) &&
            !vm_.set(tag.name, plc::Value::boolean(forced))) {
            error_ = "PLC program has no variable named '" + tag.name + "'";
            return false;
        }
    }
    return true;
}

bool SimDriver::write_outputs() {
    if (!connected_) {
        error_ = "driver is not connected";
        return false;
    }

    for (u32 index = 0; index < tags_.size(); ++index) {
        const Tag& tag = tags_.tag(index);
        if (tag.direction != TagDirection::Output) {
            continue;
        }
        bool value = vm_.get(tag.name).as_bool();

        // A forced output overrides what the program computed on its way to the
        // field. The program image keeps the computed value, so a trace shows
        // the logic and the force disagreeing, which is the point of a force.
        bool forced = false;
        if (forces_ != nullptr && forces_->find(tag.name, forced)) {
            value = forced;
        }

        if (!tags_.write_bool(index, value, plant_)) {
            error_ = "failed to write output tag '" + tag.name + "'";
            return false;
        }
    }
    return true;
}

bool SimDriver::run_scan(i64 now_ms) {
    if (!read_inputs()) {
        return false;
    }
    if (!vm_.scan(now_ms)) {
        error_ = vm_.fault_message();
        return false;
    }
    ++scans_;
    return write_outputs();
}

}  // namespace sc::io
