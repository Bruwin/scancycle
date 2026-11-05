#pragma once
// Modbus/TCP framing. Written in-house rather than pulling in libmodbus: the
// MBAP header and the two function codes we need are the whole protocol surface
// here, and owning them keeps the codec unit testable without a network. The
// socket layer lives in io/socket_io and io/modbus_tcp; this file stays pure
// framing so the wire format can be tested without opening a port.
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

// Server-side responses. They live beside the requests so a software slave and
// the frame parser cannot disagree about MBAP lengths or byte order.
std::vector<u8> build_read_response(u16 transaction, u8 unit, const std::vector<u16>& registers);
std::vector<u8> build_exception_response(u16 transaction, u8 unit, u8 function, u8 code);

// Clears `error` and returns true on a well-formed response.
bool parse_read_response(const std::vector<u8>& frame, ModbusResponse& out, std::string& error);
bool parse_write_response(const std::vector<u8>& frame, u16& address, u16& value,
                          std::string& error);

}  // namespace sc::io
