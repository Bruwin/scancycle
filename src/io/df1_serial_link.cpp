#include "io/df1_serial_link.hpp"

#include <chrono>
#include <utility>

namespace sc::io {
namespace {

// Drops leading line noise but keeps a trailing DLE, which may be the first
// half of a start sequence split across two reads.
void trim_to_frame_start(std::vector<u8>& incoming) {
    std::size_t start = 0;
    while (start + 1 < incoming.size() &&
           !(incoming[start] == kDf1Dle && incoming[start + 1] == kDf1Stx)) {
        ++start;
    }
    if (start + 1 >= incoming.size()) {
        if (!incoming.empty() && incoming.back() == kDf1Dle) {
            incoming.assign(1, kDf1Dle);
        } else {
            incoming.clear();
        }
        return;
    }
    if (start > 0) {
        incoming.erase(incoming.begin(), incoming.begin() + static_cast<std::ptrdiff_t>(start));
    }
}

}  // namespace

Df1SerialLink::Df1SerialLink(Df1SerialConfig config) : config_(std::move(config)) {}

Df1SerialLink::~Df1SerialLink() { close(); }

bool Df1SerialLink::open(std::string& error) {
    close();
    handle_ = serial_open(config_.device, config_.baud, error);
    return handle_ != kInvalidSerial;
}

void Df1SerialLink::close() {
    if (handle_ != kInvalidSerial) {
        serial_close(handle_);
        handle_ = kInvalidSerial;
    }
}

bool Df1SerialLink::exchange(std::span<const u8> request, std::vector<u8>& response,
                             std::string& error) {
    error.clear();
    response.clear();
    if (handle_ == kInvalidSerial) {
        error = "serial link is not open";
        return false;
    }
    if (!serial_write(handle_, request, error)) {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(config_.timeout_ms);
    std::vector<u8> incoming;
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            error = "no DF1 reply within " + std::to_string(config_.timeout_ms) + " ms";
            return false;
        }
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int remaining = left > 0 ? static_cast<int>(left) : 1;

        u8 chunk[128];
        bool timed_out = false;
        const int count = serial_read(handle_, chunk, remaining, timed_out, error);
        if (count < 0) {
            return false;
        }
        if (count == 0) {
            continue;  // timeout or EINTR; the deadline check above decides
        }
        incoming.insert(incoming.end(), chunk, chunk + count);

        trim_to_frame_start(incoming);
        if (incoming.empty()) {
            continue;
        }

        std::vector<u8> body;
        std::size_t consumed = 0;
        if (df1_decode(incoming, config_.checksum, body, consumed, error)) {
            response.assign(incoming.begin(),
                            incoming.begin() + static_cast<std::ptrdiff_t>(consumed));
            return true;
        }
        if (consumed > 0) {
            return false;  // a complete but invalid frame is a real error
        }
        error.clear();  // merely incomplete: keep reading
    }
}

}  // namespace sc::io
