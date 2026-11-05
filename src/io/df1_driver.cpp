#include "io/df1_driver.hpp"

#include <utility>

namespace sc::io {

Df1Driver::Df1Driver(Df1Config config, plc::Vm& vm, Df1Link& link)
    : config_(std::move(config)),
      vm_(vm),
      client_(config_.station, config_.destination, config_.checksum, link) {
    written_.assign(config_.tags.size(), 0);
    last_written_.assign(config_.tags.size(), 0);
}

Df1Driver::~Df1Driver() { disconnect(); }

bool Df1Driver::connect() {
    disconnect();
    error_.clear();
    if (config_.station > 254 || config_.destination > 254) {
        error_ = "DF1 node addresses must be in 0..254";
        return false;
    }
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
    if (!client_.open(error_)) {
        return false;
    }
    scans_ = 0;
    last_scan_ms_ = 0;
    written_.assign(config_.tags.size(), 0);
    last_written_.assign(config_.tags.size(), 0);
    connected_ = true;
    return true;
}

void Df1Driver::disconnect() {
    if (connected_) {
        client_.close();
    }
    connected_ = false;
}

bool Df1Driver::read_word(const Df1Tag& tag, u16& value) {
    return client_.read_word(tag.file, tag.file_type, tag.element, value, error_);
}

bool Df1Driver::write_word(const Df1Tag& tag, u16 value) {
    return client_.write_word(tag.file, tag.file_type, tag.element, value, error_);
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
