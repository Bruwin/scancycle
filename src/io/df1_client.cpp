#include "io/df1_client.hpp"

#include <utility>

namespace sc::io {

Df1Client::Df1Client(u8 host, u8 destination, Df1Checksum checksum, Df1Link& link)
    : link_(link), host_(host), destination_(destination), checksum_(checksum) {}

Df1Client::~Df1Client() { close(); }

bool Df1Client::open(std::string& error) {
    close();
    error.clear();
    if (!link_.open(error)) {
        return false;
    }
    transaction_ = 0;
    exchanges_ = 0;
    open_ = true;
    return true;
}

void Df1Client::close() {
    if (open_) {
        link_.close();
        open_ = false;
    }
}

bool Df1Client::transact(u8 command, std::span<const u8> payload, Df1Header& request,
                         std::vector<u8>& reply, std::string& error) {
    error.clear();
    if (!open_) {
        error = "DF1 client is not open";
        return false;
    }
    ++transaction_;
    if (transaction_ == 0) {
        ++transaction_;
    }

    request = Df1Header{};
    request.destination = destination_;
    request.source = host_;
    request.command = command;
    request.transaction = transaction_;

    std::vector<u8> body = df1_body(request);
    body.insert(body.end(), payload.begin(), payload.end());

    std::vector<u8> frame;
    if (!df1_encode(body, checksum_, frame, error)) {
        return false;
    }
    std::vector<u8> incoming;
    if (!link_.exchange(frame, incoming, error)) {
        return false;
    }
    ++exchanges_;
    if (incoming.empty()) {
        error = "controller did not answer";
        return false;
    }

    std::vector<u8> decoded;
    std::size_t consumed = 0;
    if (!df1_decode(incoming, checksum_, decoded, consumed, error)) {
        return false;
    }
    if (consumed != incoming.size()) {
        error = "DF1 reply carries trailing bytes";
        return false;
    }

    Df1Header header;
    std::size_t at = 0;
    if (!df1_parse_header(decoded, header, at, error)) {
        return false;
    }
    if (header.transaction != transaction_) {
        error = "DF1 reply transaction does not match the request";
        return false;
    }
    reply = std::move(decoded);
    return true;
}

bool Df1Client::read_word(u8 file, u8 file_type, u16 element, u16& value,
                          std::string& error) {
    const PcccAddress address{file, file_type, element, 0};
    const std::vector<u8> payload = build_pccc_read_request(address, 2);
    if (payload.empty()) {
        error = "could not build a DF1 read request";
        return false;
    }

    Df1Header request;
    std::vector<u8> reply;
    if (!transact(kDf1CmdProtectedRead, payload, request, reply, error)) {
        return false;
    }

    PcccReply parsed;
    if (!parse_pccc_read_reply(reply, request, address, parsed, error)) {
        return false;
    }
    if (parsed.words.size() != 1) {
        error = "DF1 read did not return exactly one word";
        return false;
    }
    value = parsed.words.front();
    return true;
}

bool Df1Client::write_word(u8 file, u8 file_type, u16 element, u16 value,
                           std::string& error) {
    const PcccAddress address{file, file_type, element, 0};
    const std::vector<u16> words{value};
    const std::vector<u8> payload = build_pccc_write_request(address, words);
    if (payload.empty()) {
        error = "could not build a DF1 write request";
        return false;
    }

    Df1Header request;
    std::vector<u8> reply;
    if (!transact(kDf1CmdProtectedWrite, payload, request, reply, error)) {
        return false;
    }
    return parse_pccc_write_reply(reply, request, error);
}

bool Df1Client::read_bit(u8 file, u8 file_type, u16 element, u8 bit, bool& value,
                         std::string& error) {
    if (bit > 15) {
        error = "DF1 bit must be in 0..15";
        return false;
    }
    u16 word = 0;
    if (!read_word(file, file_type, element, word, error)) {
        return false;
    }
    value = ((word >> bit) & 1u) != 0;
    return true;
}

bool Df1Client::write_bit(u8 file, u8 file_type, u16 element, u8 bit, bool value,
                          std::string& error) {
    if (bit > 15) {
        error = "DF1 bit must be in 0..15";
        return false;
    }
    u16 word = 0;
    if (!read_word(file, file_type, element, word, error)) {
        return false;
    }
    const u16 mask = static_cast<u16>(1u << bit);
    const u16 next = value ? static_cast<u16>(word | mask)
                           : static_cast<u16>(word & ~mask);
    if (next == word) {
        return true;
    }
    return write_word(file, file_type, element, next, error);
}

}  // namespace sc::io
