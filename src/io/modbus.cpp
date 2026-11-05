#include "io/modbus.hpp"

namespace sc::io {
namespace {

void push_u16(std::vector<u8>& out, u16 value) {
    out.push_back(static_cast<u8>((value >> 8) & 0xFF));
    out.push_back(static_cast<u8>(value & 0xFF));
}

u16 read_u16(const std::vector<u8>& in, std::size_t at) {
    return static_cast<u16>((static_cast<u16>(in[at]) << 8) | static_cast<u16>(in[at + 1]));
}

bool check_header(const std::vector<u8>& frame, std::string& error) {
    if (frame.size() < kMbapHeaderBytes + 2) {
        error = "frame is too short";
        return false;
    }
    if (read_u16(frame, 2) != kModbusProtocolId) {
        error = "not a Modbus frame (protocol id mismatch)";
        return false;
    }
    if (read_u16(frame, 4) != frame.size() - 6) {
        error = "MBAP length does not match the frame size";
        return false;
    }
    if ((frame[7] & 0x80) != 0) {
        error = "Modbus exception response, code " + std::to_string(frame[8]);
        return false;
    }
    return true;
}

}  // namespace

std::vector<u8> build_read_holding_registers(u16 transaction, u8 unit, u16 address, u16 count) {
    std::vector<u8> frame;
    frame.reserve(12);
    push_u16(frame, transaction);
    push_u16(frame, kModbusProtocolId);
    push_u16(frame, 6);  // unit + function + address + count
    frame.push_back(unit);
    frame.push_back(kFuncReadHoldingRegisters);
    push_u16(frame, address);
    push_u16(frame, count);
    return frame;
}

std::vector<u8> build_write_single_register(u16 transaction, u8 unit, u16 address, u16 value) {
    std::vector<u8> frame;
    frame.reserve(12);
    push_u16(frame, transaction);
    push_u16(frame, kModbusProtocolId);
    push_u16(frame, 6);
    frame.push_back(unit);
    frame.push_back(kFuncWriteSingleRegister);
    push_u16(frame, address);
    push_u16(frame, value);
    return frame;
}

bool parse_read_response(const std::vector<u8>& frame, ModbusResponse& out, std::string& error) {
    error.clear();
    if (!check_header(frame, error)) {
        return false;
    }
    if (frame[7] != kFuncReadHoldingRegisters) {
        error = "unexpected function code in a read response";
        return false;
    }
    const std::size_t byte_count = frame[8];
    if (frame.size() != 9 + byte_count) {
        error = "byte count does not match the response size";
        return false;
    }
    if ((byte_count % 2) != 0) {
        error = "register payload is not a whole number of words";
        return false;
    }

    out.transaction = read_u16(frame, 0);
    out.unit = frame[6];
    out.registers.clear();
    out.registers.reserve(byte_count / 2);
    for (std::size_t at = 9; at + 1 < frame.size(); at += 2) {
        out.registers.push_back(read_u16(frame, at));
    }
    return true;
}

bool parse_write_response(const std::vector<u8>& frame, u16& address, u16& value,
                          std::string& error) {
    error.clear();
    if (!check_header(frame, error)) {
        return false;
    }
    if (frame[7] != kFuncWriteSingleRegister) {
        error = "unexpected function code in a write response";
        return false;
    }
    if (frame.size() != 12) {
        error = "write response must be exactly 12 bytes";
        return false;
    }
    address = read_u16(frame, 8);
    value = read_u16(frame, 10);
    return true;
}

}  // namespace sc::io
