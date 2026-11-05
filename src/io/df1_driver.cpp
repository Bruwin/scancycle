#include "io/df1_driver.hpp"

#include <utility>

namespace sc::io {

Df1Driver::Df1Driver(Df1Config config, plc::Vm& vm, Df1Link& link)
    : config_(std::move(config)), vm_(vm), link_(link) {
    written_.assign(config_.tags.size(), 0);
    last_written_.assign(config_.tags.size(), 0);
}

Df1Driver::~Df1Driver() { disconnect(); }

bool Df1Driver::connect() {
    disconnect();
    error_.clear();
    for (const Df1Tag& tag : config_.tags) {
        if (tag.bit > 15) {
            error_ = "tag '" + tag.name + "' uses a bit outside 0..15";
            return false;
        }
        if (tag.file_type != kPcccFileTypeInteger &&
            tag.file_type != kPcccFileTypeBinary) {
            error_ = "tag '" + tag.name + "' uses an unsupported file type";
            return false;
        }
    }
    if (!link_.open(error_)) {
        return false;
    }
    transaction_ = 0;
    scans_ = 0;
    last_scan_ms_ = 0;
    exchanges_ = 0;
    written_.assign(config_.tags.size(), 0);
    last_written_.assign(config_.tags.size(), 0);
    connected_ = true;
    return true;
}

void Df1Driver::disconnect() {
    if (connected_) {
        link_.close();
    }
    connected_ = false;
}

bool Df1Driver::transact(u8 command, std::span<const u8> payload,
                         Df1Header& request_header, std::vector<u8>& reply_body) {
    if (!connected_) {
        error_ = "driver is not connected";
        return false;
    }
    ++transaction_;
    if (transaction_ == 0) {
        ++transaction_;
    }

    request_header = Df1Header{};
    request_header.destination = config_.destination;
    request_header.source = config_.station;
    request_header.command = command;
    request_header.transaction = transaction_;

    std::vector<u8> body = df1_body(request_header);
    body.insert(body.end(), payload.begin(), payload.end());

    std::vector<u8> frame;
    if (!df1_encode(body, config_.checksum, frame, error_)) {
        return false;
    }
    std::vector<u8> reply;
    if (!link_.exchange(frame, reply, error_)) {
        return false;
    }
    ++exchanges_;
    if (reply.empty()) {
        error_ = "controller did not answer";
        return false;
    }

    std::size_t consumed = 0;
    if (!df1_decode(reply, config_.checksum, reply_body, consumed, error_)) {
        return false;
    }
    if (consumed != reply.size()) {
        error_ = "DF1 reply carries trailing bytes";
        return false;
    }

    Df1Header reply_header;
    std::size_t at = 0;
    if (!df1_parse_header(reply_body, reply_header, at, error_)) {
        return false;
    }
    if (reply_header.transaction != transaction_) {
        error_ = "DF1 reply transaction does not match the request";
        return false;
    }
    return true;
}

bool Df1Driver::read_word(const Df1Tag& tag, u16& value) {
    const PcccAddress address{tag.file, tag.file_type, tag.element, 0};
    const std::vector<u8> payload = build_pccc_read_request(address, 2);
    if (payload.empty()) {
        error_ = "could not build a read request for '" + tag.name + "'";
        return false;
    }

    Df1Header request;
    std::vector<u8> reply;
    if (!transact(kDf1CmdProtectedRead, payload, request, reply)) {
        return false;
    }

    PcccReply parsed;
    if (!parse_pccc_read_reply(reply, request, address, parsed, error_)) {
        return false;
    }
    if (parsed.words.size() != 1) {
        error_ = "read of '" + tag.name + "' did not return exactly one word";
        return false;
    }
    value = parsed.words.front();
    return true;
}

bool Df1Driver::write_word(const Df1Tag& tag, u16 value) {
    const PcccAddress address{tag.file, tag.file_type, tag.element, 0};
    const std::vector<u16> words{value};
    const std::vector<u8> payload = build_pccc_write_request(address, words);
    if (payload.empty()) {
        error_ = "could not build a write request for '" + tag.name + "'";
        return false;
    }

    Df1Header request;
    std::vector<u8> reply;
    if (!transact(kDf1CmdProtectedWrite, payload, request, reply)) {
        return false;
    }
    return parse_pccc_write_reply(reply, request, address, error_);
}

bool Df1Driver::read_inputs() {
    error_.clear();
    if (!connected_) {
        error_ = "driver is not connected";
        return false;
    }
    for (const Df1Tag& tag : config_.tags) {
        if (tag.direction != Df1Direction::Input) {
            continue;
        }
        u16 word = 0;
        if (!read_word(tag, word)) {
            return false;
        }
        const bool value = ((word >> tag.bit) & 1u) != 0;
        if (!vm_.set(tag.name, plc::Value::boolean(value))) {
            error_ = "PLC program has no variable named '" + tag.name + "'";
            return false;
        }
    }
    return true;
}

bool Df1Driver::write_outputs_at(i64 now_ms) {
    if (!connected_) {
        error_ = "driver is not connected";
        return false;
    }
    for (std::size_t index = 0; index < config_.tags.size(); ++index) {
        const Df1Tag& tag = config_.tags[index];
        if (tag.direction != Df1Direction::Output) {
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

        // Read-modify-write keeps the other fifteen bits of the word intact.
        u16 current = 0;
        if (!read_word(tag, current)) {
            return false;
        }
        const u16 mask = static_cast<u16>(1u << tag.bit);
        const u16 next = value != 0 ? static_cast<u16>(current | mask)
                                    : static_cast<u16>(current & ~mask);
        if (next != current) {
            if (config_.commissioning == nullptr) {
                error_ = "no commissioning policy for write to '" + tag.name + "'";
                return false;
            }
            if (!config_.commissioning->can_write(tag.name, now_ms)) {
                error_ = "commissioning gate denied write to '" + tag.name + "'";
                return false;
            }
            if (!write_word(tag, next)) {
                return false;
            }
        }
        written_[index] = 1;
        last_written_[index] = value;
    }
    return true;
}

bool Df1Driver::write_outputs() { return write_outputs_at(last_scan_ms_); }

bool Df1Driver::run_scan(i64 now_ms) {
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
