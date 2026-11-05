#include "io/df1.hpp"

namespace sc::io {

u8 df1_bcc(std::span<const u8> body) {
    u32 sum = 0;
    for (const u8 byte : body) {
        sum += byte;
    }
    return static_cast<u8>(0x100u - (sum & 0xFFu));
}

u16 df1_crc16(std::span<const u8> body) {
    u16 crc = 0;
    for (const u8 byte : body) {
        crc = static_cast<u16>(crc ^ byte);
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 1u) != 0) {
                crc = static_cast<u16>((crc >> 1) ^ 0xA001u);
            } else {
                crc = static_cast<u16>(crc >> 1);
            }
        }
    }
    return crc;
}

bool df1_encode(std::span<const u8> body, Df1Checksum checksum, std::vector<u8>& frame,
                std::string& error) {
    error.clear();
    frame.clear();
    if (body.empty()) {
        error = "DF1 body is empty";
        return false;
    }
    if (body.size() > kDf1MaxBodyBytes) {
        error = "DF1 body is too long";
        return false;
    }

    frame.reserve(body.size() + 8);
    frame.push_back(kDf1Dle);
    frame.push_back(kDf1Stx);
    for (const u8 byte : body) {
        frame.push_back(byte);
        if (byte == kDf1Dle) {
            frame.push_back(kDf1Dle);
        }
    }
    frame.push_back(kDf1Dle);
    frame.push_back(kDf1Etx);
    if (checksum == Df1Checksum::Bcc) {
        frame.push_back(df1_bcc(body));
    } else {
        const u16 crc = df1_crc16(body);
        frame.push_back(static_cast<u8>(crc & 0xFFu));
        frame.push_back(static_cast<u8>(crc >> 8));
    }
    return true;
}

bool df1_decode(std::span<const u8> bytes, Df1Checksum checksum, std::vector<u8>& body,
                std::size_t& consumed, std::string& error) {
    error.clear();
    body.clear();
    consumed = 0;

    std::size_t start = 0;
    bool found = false;
    while (start + 1 < bytes.size()) {
        if (bytes[start] == kDf1Dle && bytes[start + 1] == kDf1Stx) {
            found = true;
            break;
        }
        ++start;
    }
    if (!found) {
        error = "no DF1 frame start";
        consumed = bytes.size();
        // A trailing DLE may be the first half of the next start sequence.
        if (consumed > 0 && bytes[consumed - 1] == kDf1Dle) {
            --consumed;
        }
        return false;
    }

    std::size_t at = start + 2;
    for (;;) {
        if (at >= bytes.size()) {
            error = "DF1 frame is incomplete";
            return false;
        }
        if (body.size() >= kDf1MaxBodyBytes) {
            error = "DF1 body is too long";
            body.clear();
            consumed = at;
            return false;
        }

        const u8 byte = bytes[at];
        if (byte != kDf1Dle) {
            body.push_back(byte);
            ++at;
            continue;
        }
        if (at + 1 >= bytes.size()) {
            error = "DF1 frame is incomplete";
            return false;
        }
        const u8 next = bytes[at + 1];
        if (next == kDf1Dle) {
            body.push_back(kDf1Dle);
            at += 2;
            continue;
        }
        if (next == kDf1Stx) {
            // A fresh start sequence: whatever came before it was noise.
            body.clear();
            at += 2;
            continue;
        }
        if (next != kDf1Etx) {
            error = "unexpected DF1 control byte inside a frame";
            body.clear();
            consumed = at + 2;
            return false;
        }

        const std::size_t check_at = at + 2;
        const std::size_t check_bytes = checksum == Df1Checksum::Bcc ? 1 : 2;
        if (check_at + check_bytes > bytes.size()) {
            error = "DF1 frame is incomplete";
            return false;
        }

        bool valid = false;
        if (checksum == Df1Checksum::Bcc) {
            valid = df1_bcc(body) == bytes[check_at];
        } else {
            const u16 crc = df1_crc16(body);
            valid = static_cast<u8>(crc & 0xFFu) == bytes[check_at] &&
                    static_cast<u8>(crc >> 8) == bytes[check_at + 1];
        }
        consumed = check_at + check_bytes;
        if (!valid) {
            error = "DF1 checksum mismatch";
            body.clear();
            return false;
        }
        return true;
    }
}

std::vector<u8> df1_body(const Df1Header& header) {
    std::vector<u8> body;
    body.reserve(6);
    body.push_back(header.destination);
    body.push_back(header.source);
    body.push_back(header.command);
    body.push_back(header.status);
    body.push_back(static_cast<u8>(header.transaction & 0xFFu));
    body.push_back(static_cast<u8>(header.transaction >> 8));
    return body;
}

bool df1_parse_header(std::span<const u8> body, Df1Header& header, std::size_t& payload_at,
                      std::string& error) {
    error.clear();
    if (body.size() < 6) {
        error = "DF1 body is shorter than its header";
        return false;
    }
    header.destination = body[0];
    header.source = body[1];
    header.command = body[2];
    header.status = body[3];
    header.transaction = static_cast<u16>(body[4] | (static_cast<u16>(body[5]) << 8));
    payload_at = 6;
    return true;
}

}  // namespace sc::io
