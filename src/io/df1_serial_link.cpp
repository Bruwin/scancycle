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

// Full-duplex DF1 answers a frame with DLE ACK or DLE NAK before any reply
// frame. Both are two bytes carrying no payload; a literal 0x10 inside a frame
// is always doubled or followed by ETX, so these pairs can only be control
// bytes here.
constexpr u8 kDf1Ack = 0x06;
constexpr u8 kDf1Nak = 0x15;

enum class Control { None, Ack, Nak };

Control take_control(std::vector<u8>& incoming) {
    if (incoming.size() < 2 || incoming[0] != kDf1Dle) {
        return Control::None;
    }
    const u8 next = incoming[1];
    if (next != kDf1Ack && next != kDf1Nak) {
        return Control::None;
    }
    incoming.erase(incoming.begin(), incoming.begin() + 2);
    return next == kDf1Ack ? Control::Ack : Control::Nak;
}

bool send_control(SerialHandle handle, u8 control, std::string& error) {
    const u8 bytes[2] = {kDf1Dle, control};
    return serial_write(handle, bytes, error);
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
    bool acknowledged = false;
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            // These mean different things: one says the line or the settings are
            // wrong, the other says the controller heard us and the reply never
            // came back.
            error = acknowledged
                        ? "the controller acknowledged the request but sent no reply within " +
                              std::to_string(config_.timeout_ms) + " ms"
                        : "no DF1 reply within " + std::to_string(config_.timeout_ms) + " ms";
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

        for (;;) {
            const Control control = take_control(incoming);
            if (control == Control::None) {
                break;
            }
            if (control == Control::Nak) {
                error = "the controller rejected the frame (DLE NAK)";
                return false;
            }
            acknowledged = true;  // it heard the request; the reply comes next
        }

        trim_to_frame_start(incoming);
        if (incoming.empty()) {
            continue;
        }

        std::vector<u8> body;
        std::size_t consumed = 0;
        if (df1_decode(incoming, config_.checksum, body, consumed, error)) {
            response.assign(incoming.begin(),
                            incoming.begin() + static_cast<std::ptrdiff_t>(consumed));
            // A controller expects its reply acknowledged. Staying silent makes
            // it retransmit and eventually give up on the link.
            return send_control(handle_, kDf1Ack, error);
        }
        if (consumed > 0) {
            // A complete but invalid frame earns the answer a controller gives,
            // so the peer resends instead of repeating it into a link that looks
            // dead from here.
            std::string ignored;
            send_control(handle_, kDf1Nak, ignored);
            return false;  // a complete but invalid frame is a real error
        }
        error.clear();  // merely incomplete: keep reading
    }
}

}  // namespace sc::io
