#include "io/df1_pccc.hpp"

namespace sc::io {
namespace {

void push_address(std::vector<u8>& payload, const PcccAddress& address) {
    payload.push_back(address.file);
    payload.push_back(address.file_type);
    payload.push_back(static_cast<u8>(address.element & 0xFFu));
    payload.push_back(static_cast<u8>(address.element >> 8));
    payload.push_back(address.sub_element);
}

std::string hex_byte(u8 value) {
    constexpr char kDigits[] = "0123456789ABCDEF";
    std::string text = "0x";
    text.push_back(kDigits[value >> 4]);
    text.push_back(kDigits[value & 0x0Fu]);
    return text;
}

u16 read_u16_at(std::span<const u8> bytes, std::size_t at) {
    return static_cast<u16>(bytes[at] | (static_cast<u16>(bytes[at + 1]) << 8));
}

bool read_byte_count(std::size_t words, u8& count) {
    const std::size_t bytes = words * 2;
    if (bytes == 0 || bytes > kPcccMaxDataBytes) {
        return false;
    }
    count = static_cast<u8>(bytes);
    return true;
}

// Shared reply prefix: header, command, transaction, status and address echo.
bool parse_reply_prefix(std::span<const u8> body, const Df1Header& expected, u8 command,
                        const PcccAddress& address, Df1Header& header, std::size_t& at,
                        std::string& error) {
    error.clear();
    if (!df1_parse_header(body, header, at, error)) {
        return false;
    }
    if (header.command != command) {
        error = "DF1 reply has an unexpected command byte";
        return false;
    }
    if (header.transaction != expected.transaction) {
        error = "DF1 reply transaction does not match the request";
        return false;
    }
    if (header.status != kPcccStatusOk) {
        error = "controller returned status " + hex_byte(header.status);
        return false;
    }
    if (body.size() < at + 7) {
        error = "PCCC reply is shorter than its address block";
        return false;
    }
    const PcccAddress echoed{body[at + 2], body[at + 3], read_u16_at(body, at + 4),
                             body[at + 6]};
    if (!(echoed == address)) {
        error = "PCCC reply address does not match the request";
        return false;
    }
    return true;
}

}  // namespace

std::vector<u8> build_pccc_read_request(const PcccAddress& address, u8 byte_count) {
    if (byte_count == 0 || (byte_count % 2) != 0 || byte_count > kPcccMaxDataBytes) {
        return {};
    }
    std::vector<u8> payload;
    payload.reserve(7);
    payload.push_back(kPcccFuncRead);
    payload.push_back(byte_count);
    push_address(payload, address);
    return payload;
}

std::vector<u8> build_pccc_write_request(const PcccAddress& address,
                                         std::span<const u16> words) {
    u8 byte_count = 0;
    if (!read_byte_count(words.size(), byte_count)) {
        return {};
    }
    std::vector<u8> payload;
    payload.reserve(7 + byte_count);
    payload.push_back(kPcccFuncWrite);
    payload.push_back(byte_count);
    push_address(payload, address);
    for (const u16 word : words) {
        payload.push_back(static_cast<u8>(word & 0xFFu));
        payload.push_back(static_cast<u8>(word >> 8));
    }
    return payload;
}

std::vector<u8> build_pccc_read_reply(const PcccAddress& address,
                                      std::span<const u16> words) {
    u8 byte_count = 0;
    if (!read_byte_count(words.size(), byte_count)) {
        return {};
    }
    std::vector<u8> payload;
    payload.reserve(7 + byte_count);
    payload.push_back(kPcccFuncReadReply);
    payload.push_back(byte_count);
    push_address(payload, address);
    for (const u16 word : words) {
        payload.push_back(static_cast<u8>(word & 0xFFu));
        payload.push_back(static_cast<u8>(word >> 8));
    }
    return payload;
}

std::vector<u8> build_pccc_write_reply(const PcccAddress& address) {
    std::vector<u8> payload;
    payload.reserve(7);
    payload.push_back(kPcccFuncWriteReply);
    payload.push_back(0);
    push_address(payload, address);
    return payload;
}

std::vector<u8> build_pccc_error_reply(u8 function) { return std::vector<u8>{function}; }

bool parse_pccc_read_reply(std::span<const u8> body, const Df1Header& expected,
                           const PcccAddress& address, PcccReply& out, std::string& error) {
    out = PcccReply{};
    Df1Header header;
    std::size_t at = 0;
    if (!parse_reply_prefix(body, expected, kDf1CmdReadReply, address, header, at, error)) {
        return false;
    }
    if (body[at] != kPcccFuncReadReply) {
        error = "PCCC reply has an unexpected function byte";
        return false;
    }
    const std::size_t size = body[at + 1];
    if ((size % 2) != 0 || body.size() != at + 7 + size) {
        error = "PCCC read reply length does not match its size byte";
        return false;
    }

    out.header = header;
    out.address = address;
    out.words.reserve(size / 2);
    for (std::size_t offset = 0; offset < size; offset += 2) {
        out.words.push_back(read_u16_at(body, at + 7 + offset));
    }
    return true;
}

bool parse_pccc_write_reply(std::span<const u8> body, const Df1Header& expected,
                            const PcccAddress& address, std::string& error) {
    Df1Header header;
    std::size_t at = 0;
    if (!parse_reply_prefix(body, expected, kDf1CmdWriteReply, address, header, at, error)) {
        return false;
    }
    if (body[at] != kPcccFuncWriteReply) {
        error = "PCCC reply has an unexpected function byte";
        return false;
    }
    if (body.size() != at + 7) {
        error = "PCCC write reply carries unexpected data";
        return false;
    }
    return true;
}

}  // namespace sc::io
