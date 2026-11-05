#pragma once
// The byte stream a DF1 driver runs on. The loopback software peer in
// io/df1_slave is one implementation; a serial port or a serial-over-TCP
// gateway implements the same three calls, which is why the driver never
// mentions termios or sockets. exchange() is one framed request and its framed
// reply: DF1 full-duplex is turn-based, so a driver never needs to read while
// it writes.
#include "core/types.hpp"

#include <span>
#include <string>
#include <vector>

namespace sc::io {

class Df1Link {
public:
    virtual ~Df1Link() = default;

    virtual bool open(std::string& error) = 0;
    virtual void close() = 0;

    // On success `response` holds the reply frame. An empty response with a
    // true return means the peer stayed silent, which the driver reports as a
    // timeout rather than a framing error. A link that can see DF1's link-layer
    // control bytes reports a DLE NAK as a failure with a diagnostic, so a frame
    // the controller rejected never reaches the caller looking like silence.
    virtual bool exchange(std::span<const u8> request, std::vector<u8>& response,
                          std::string& error) = 0;
};

}  // namespace sc::io
