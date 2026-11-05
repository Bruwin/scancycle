#pragma once
// PCCC protected typed logical read/write, the SLC 500 and PLC-5 command family
// that serial DF1 carries. One element range per request, one file type per
// range, and data words in the little-endian order the controller replies with.
// This file owns payload bytes only: io/df1 wraps them in a DLE frame and
// io/df1_driver decides when to send them.
#include "core/types.hpp"
#include "io/df1.hpp"

#include <span>
#include <string>
#include <vector>

namespace sc::io {

// 0x0F/0x10 are command bytes in the DF1 header. The function byte inside the
// payload is different: 0xA2 for a protected typed logical read and 0xAA for a
// write, and a reply echoes that same function byte. Sending the command byte in
// this slot is a malformed command that controllers silently ignore.
inline constexpr u8 kPcccFuncRead = 0xA2;
inline constexpr u8 kPcccFuncWrite = 0xAA;
inline constexpr u8 kPcccFuncReadReply = kPcccFuncRead;
inline constexpr u8 kPcccFuncWriteReply = kPcccFuncWrite;

// File type identifiers, not file numbers: N7 is file 7 with type 0x89.
inline constexpr u8 kPcccFileTypeBinary = 0x85;
inline constexpr u8 kPcccFileTypeInteger = 0x89;

inline constexpr u8 kPcccStatusOk = 0x00;
// The controller understood the command but could not carry it out, for
// example because the file is missing or the element is out of range.
inline constexpr u8 kPcccStatusError = 0xF0;

// A DF1 frame carries at most 242 data bytes in one PCCC request or reply.
inline constexpr u16 kPcccMaxDataBytes = 242;

struct PcccAddress {
    u8 file = 0;
    u8 file_type = kPcccFileTypeInteger;
    u16 element = 0;
    u8 sub_element = 0;

    bool operator==(const PcccAddress&) const = default;
};

struct PcccReply {
    Df1Header header;
    PcccAddress address;
    std::vector<u16> words;
};

// Payloads, ready to follow df1_body(). `byte_count` is the number of data
// bytes the controller must return: even, non-zero and at most 242. Builders
// return an empty vector rather than truncating a request that does not fit.
std::vector<u8> build_pccc_read_request(const PcccAddress& address, u8 byte_count);
std::vector<u8> build_pccc_write_request(const PcccAddress& address,
                                         std::span<const u16> words);
std::vector<u8> build_pccc_read_reply(const PcccAddress& address, std::span<const u16> words);
std::vector<u8> build_pccc_write_reply(const PcccAddress& address);
std::vector<u8> build_pccc_error_reply(u8 function);

// Parsers take a complete DF1 body (header included) and reject a reply whose
// command, transaction, status or address does not match the request.
bool parse_pccc_read_reply(std::span<const u8> body, const Df1Header& expected,
                           const PcccAddress& address, PcccReply& out, std::string& error);
bool parse_pccc_write_reply(std::span<const u8> body, const Df1Header& expected,
                            const PcccAddress& address, std::string& error);

}  // namespace sc::io
