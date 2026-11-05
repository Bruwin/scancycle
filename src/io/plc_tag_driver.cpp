#include "io/plc_tag_driver.hpp"

#include <utility>

namespace sc::io {

LibPlcTagDriver::LibPlcTagDriver(LibPlcTagConfig config, plc::Vm& vm,
                                 std::unique_ptr<PlcTagApi> api)
    : config_(std::move(config)), vm_(vm), api_(std::move(api)) {
    handles_.assign(config_.tags.size(), 0);
    written_.assign(config_.tags.size(), 0);
    last_written_.assign(config_.tags.size(), 0);
}

LibPlcTagDriver::~LibPlcTagDriver() { disconnect(); }

bool LibPlcTagDriver::connect() {
    disconnect();
    error_.clear();
    if (!api_) {
        error_ = "libplctag API is unavailable";
        return false;
    }

    for (std::size_t index = 0; index < config_.tags.size(); ++index) {
        const PlcTagBinding& tag = config_.tags[index];
        std::string detail;
        const i32 handle = api_->create(tag.tag, config_.timeout_ms, detail);
        if (handle <= 0) {
            error_ = "could not create tag '" + tag.name + "'";
            if (!detail.empty()) {
                error_ += ": " + detail;
            }
            disconnect();
            return false;
        }
        handles_[index] = handle;
    }
    written_.assign(config_.tags.size(), 0);
    last_written_.assign(config_.tags.size(), 0);
    scans_ = 0;
    last_scan_ms_ = 0;
    connected_ = true;
    return true;
}

void LibPlcTagDriver::disconnect() {
    if (api_) {
        for (const i32 handle : handles_) {
            if (handle > 0) {
                api_->destroy(handle);
            }
        }
    }
    handles_.assign(handles_.size(), 0);
    connected_ = false;
}

bool LibPlcTagDriver::read_inputs() {
    error_.clear();
    if (!connected_ || !api_) {
        error_ = "driver is not connected";
        return false;
    }
    for (std::size_t index = 0; index < config_.tags.size(); ++index) {
        const PlcTagBinding& tag = config_.tags[index];
        if (tag.direction != PlcTagDirection::Input) {
            continue;
        }
        if (!api_->read(handles_[index], config_.timeout_ms, error_)) {
            return false;
        }
        bool value = false;
        if (!api_->get_bool(handles_[index], value, error_)) {
            return false;
        }
        if (!vm_.set(tag.name, plc::Value::boolean(value))) {
            error_ = "PLC program has no variable named '" + tag.name + "'";
            return false;
        }
    }
    return true;
}

bool LibPlcTagDriver::write_outputs_at(i64 now_ms) {
    if (!connected_ || !api_) {
        error_ = "driver is not connected";
        return false;
    }
    for (std::size_t index = 0; index < config_.tags.size(); ++index) {
        const PlcTagBinding& tag = config_.tags[index];
        if (tag.direction != PlcTagDirection::Output) {
            continue;
        }
        const plc::Value image = vm_.get(tag.name);
        if (image.type == plc::ValueType::Void) {
            error_ = "PLC program has no variable named '" + tag.name + "'";
            return false;
        }
        const u8 value = image.as_bool() ? 1 : 0;
        if (written_[index] != 0 && last_written_[index] == value) {
            continue;
        }
        if (config_.commissioning == nullptr) {
            error_ = "no commissioning policy for write to '" + tag.name + "'";
            return false;
        }
        if (!config_.commissioning->can_write(tag.name, now_ms)) {
            error_ = "commissioning gate denied write to '" + tag.name + "'";
            return false;
        }
        if (!api_->set_bool(handles_[index], value != 0, error_)) {
            return false;
        }
        if (!api_->write(handles_[index], config_.timeout_ms, error_)) {
            return false;
        }
        written_[index] = 1;
        last_written_[index] = value;
    }
    return true;
}

bool LibPlcTagDriver::write_outputs() { return write_outputs_at(last_scan_ms_); }

bool LibPlcTagDriver::run_scan(i64 now_ms) {
    error_.clear();
    if (!read_inputs()) {
        return false;
    }
    if (!vm_.scan(now_ms)) {
        error_ = vm_.fault_message();
        return false;
    }
    ++scans_;
    last_scan_ms_ = now_ms;
    return write_outputs_at(now_ms);
}

}  // namespace sc::io
