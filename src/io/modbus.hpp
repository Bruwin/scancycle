#pragma once
// Modbus/TCP framing. Written in-house rather than pulling in libmodbus, whose
// LGPL would complicate static linking into the game binary. Only the MBAP
// header and the two function codes we need are implemented; the socket layer
// sits behind PlcDriver and is deliberately absent here so the codec is unit
// testable without a network.
#include "core/types.hpp"

#include <string>
#include <vector>

namespace sc::io {

inline constexpr u16 kModbusProtocolId = 0;
inline constexpr u8 kFuncReadHoldingRegisters = 3;
inline constexpr u8 kFuncWriteSingleRegister = 6;
inline constexpr std::size_t kMbapHeaderBytes = 7;

struct ModbusResponse {
    u16 transaction = 0;
    u8 unit = 0;
    std::vector<u16> registers;
};

std::vector<u8> build_read_holding_registers(u16 transaction, u8 unit, u16 address, u16 count);
std::vector<u8> build_write_single_register(u16 transaction, u8 unit, u16 address, u16 value);

// Clears `error` and returns true on a well-formed response.
bool parse_read_response(const std::vector<u8>& frame, ModbusResponse& out, std::string& error);
bool parse_write_response(const std::vector<u8>& frame, u16& address, u16& value,
                          std::string& error);

}  // namespace sc::io
