#include "io/modbus_tcp.hpp"

#include "io/modbus.hpp"

#include <array>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sc::io {

ModbusTcpDriver::ModbusTcpDriver(ModbusTcpConfig config, plc::Vm& vm)
    : config_(std::move(config)), vm_(vm) {
    written_.assign(config_.tags.size(), 0);
    last_written_.assign(config_.tags.size(), 0);
}

ModbusTcpDriver::~ModbusTcpDriver() { disconnect(); }

bool ModbusTcpDriver::connect() {
    disconnect();
    error_.clear();
    if (!socket_startup(error_)) {
        return false;
    }
    socket_ = connect_tcp(config_.host, config_.port, config_.timeout_ms, error_);
    if (socket_ == kInvalidSocket) {
        return false;
    }
    transaction_ = 0;
    scans_ = 0;
    last_scan_ms_ = 0;
    written_.assign(config_.tags.size(), 0);
    last_written_.assign(config_.tags.size(), 0);
    return true;
}

void ModbusTcpDriver::disconnect() {
    if (socket_ != kInvalidSocket) {
        socket_shutdown(socket_);
        socket_close(socket_);
        socket_ = kInvalidSocket;
    }
}

bool ModbusTcpDriver::connected() const { return socket_ != kInvalidSocket; }

u16 ModbusTcpDriver::next_transaction() {
    ++transaction_;
    if (transaction_ == 0) {
        ++transaction_;
    }
    return transaction_;
}

bool ModbusTcpDriver::exchange(const std::vector<u8>& request, std::vector<u8>& response) {
    if (!connected()) {
        error_ = "driver is not connected";
        return false;
    }
    if (!socket_send(socket_, request, error_)) {
        return false;
    }

    std::array<u8, kMbapHeaderBytes> header{};
    if (!socket_receive(socket_, header, error_)) {
        return false;
    }
    const u16 length =
        static_cast<u16>((static_cast<u16>(header[4]) << 8) | static_cast<u16>(header[5]));
    if (length < 2 || length > 254) {
        error_ = "Modbus response has an invalid MBAP length";
        return false;
    }

    response.assign(header.begin(), header.end());
    response.resize(6 + length);
    if (!socket_receive(socket_, std::span<u8>(response).subspan(kMbapHeaderBytes), error_)) {
        return false;
    }
    return true;
}

bool ModbusTcpDriver::read_register(u16 address, u16& value) {
    const u16 transaction = next_transaction();
    const std::vector<u8> request =
        build_read_holding_registers(transaction, config_.unit, address, 1);
    std::vector<u8> frame;
    if (!exchange(request, frame)) {
        return false;
    }

    ModbusResponse parsed;
    std::string parse_error;
    if (!parse_read_response(frame, parsed, parse_error)) {
        error_ = "read at " + std::to_string(address) + " failed: " + parse_error;
        return false;
    }
    if (parsed.transaction != transaction || parsed.unit != config_.unit) {
        error_ = "read at " + std::to_string(address) + " returned a mismatched MBAP identity";
        return false;
    }
    if (parsed.registers.size() != 1) {
        error_ = "read at " + std::to_string(address) + " did not return one register";
        return false;
    }
    value = parsed.registers.front();
    return true;
}

bool ModbusTcpDriver::write_register(u16 address, u16 value) {
    const u16 transaction = next_transaction();
    const std::vector<u8> request =
        build_write_single_register(transaction, config_.unit, address, value);
    std::vector<u8> frame;
    if (!exchange(request, frame)) {
        return false;
    }

    u16 response_address = 0;
    u16 response_value = 0;
    std::string parse_error;
    if (!parse_write_response(frame, response_address, response_value, parse_error)) {
        error_ = "write at " + std::to_string(address) + " failed: " + parse_error;
        return false;
    }
    if (response_address != address || response_value != value) {
        error_ = "write at " + std::to_string(address) + " did not echo the request";
        return false;
    }
    return true;
}

bool ModbusTcpDriver::read_inputs() {
    error_.clear();
    if (!connected()) {
        error_ = "driver is not connected";
        return false;
    }
    for (const ModbusTag& tag : config_.tags) {
        if (tag.direction != ModbusTagDirection::Input) {
            continue;
        }
        u16 raw = 0;
        if (!read_register(tag.address, raw)) {
            return false;
        }
        if (!vm_.set(tag.name, plc::Value::boolean(raw != 0))) {
            error_ = "PLC program has no variable named '" + tag.name + "'";
            return false;
        }
    }
    return true;
}

bool ModbusTcpDriver::write_outputs_at(i64 now_ms) {
    if (!connected()) {
        error_ = "driver is not connected";
        return false;
    }
    for (std::size_t index = 0; index < config_.tags.size(); ++index) {
        const ModbusTag& tag = config_.tags[index];
        if (tag.direction != ModbusTagDirection::Output) {
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
        if (!write_register(tag.address, value)) {
            return false;
        }
        written_[index] = 1;
        last_written_[index] = value;
    }
    return true;
}

bool ModbusTcpDriver::write_outputs() { return write_outputs_at(last_scan_ms_); }

bool ModbusTcpDriver::run_scan(i64 now_ms) {
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
