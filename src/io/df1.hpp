#pragma once
// DF1 full-duplex framing, in-house. A frame is DLE STX <body> DLE ETX <check>,
// and every 0x10 in the body is doubled on the wire. The body starts with the
// destination and source stations, command, status and a 16-bit transaction
// number; the PCCC payload that follows lives in io/df1_pccc. Framing stays
// free of any transport, which is what lets the wire format be tested without a
// serial port. DF1 links carry either a one-byte BCC or a two-byte CRC-16, so
// the check is a parameter rather than a hard-coded choice.
#include "core/types.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace sc::io {

inline constexpr u8 kDf1Dle = 0x10;
inline constexpr u8 kDf1Stx = 0x02;
inline constexpr u8 kDf1Etx = 0x03;
inline constexpr u8 kDf1Soh = 0x01;  // half-duplex header, not implemented yet

// Command bytes. A reply adds 0x40 to the command it answers.
inline constexpr u8 kDf1CmdProtectedRead = 0x0F;
inline constexpr u8 kDf1CmdProtectedWrite = 0x10;
inline constexpr u8 kDf1CmdReadReply = 0x4F;
inline constexpr u8 kDf1CmdWriteReply = 0x50;

// 242 PCCC data bytes plus the 6-byte header, with room for future commands.
inline constexpr std::size_t kDf1MaxBodyBytes = 512;

enum class Df1Checksum : u8 { Bcc = 0, Crc16 };

struct Df1Header {
    u8 destination = 0;
    u8 source = 1;
    u8 command = 0;
    u8 status = 0;
    u16 transaction = 0;
};

// Two's complement of the 8-bit sum of the body: adding the check byte to that
// sum yields zero, which is what a BCC link expects.
u8 df1_bcc(std::span<const u8> body);

// CRC-16/ARC: reflected polynomial 0xA001, zero initial value, low byte first.
// DF1 uses this polynomial with a zero seed; Modbus uses the same polynomial
// with a 0xFFFF seed, so the two checksums are not interchangeable. This is the
// bare register operation, useful for checking a vector on its own.
u16 df1_crc16(std::span<const u8> body);

// The check DF1 actually puts on the wire: the same register with the
// terminating ETX fed through it after the body. The DLE before ETX and the
// leading DLE STX are excluded. Omitting the ETX still round-trips against
// ourselves, which is why the specification's sample frame is a test.
u16 df1_message_crc(std::span<const u8> body);

bool df1_encode(std::span<const u8> body, Df1Checksum checksum, std::vector<u8>& frame,
                std::string& error);

// Extracts one frame from the front of `bytes`. `consumed` says how many input
// bytes belong to it, so a stream reader can drop them and keep the rest. On
// failure `consumed` is the leading noise that can be discarded (0 when the
// frame is merely incomplete and more bytes are needed).
bool df1_decode(std::span<const u8> bytes, Df1Checksum checksum, std::vector<u8>& body,
                std::size_t& consumed, std::string& error);

std::vector<u8> df1_body(const Df1Header& header);

// Splits a body into its header and payload. `payload_at` is 6 on success.
bool df1_parse_header(std::span<const u8> body, Df1Header& header, std::size_t& payload_at,
                      std::string& error);

}  // namespace sc::io
