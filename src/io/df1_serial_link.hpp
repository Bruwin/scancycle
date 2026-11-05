#pragma once
// DF1 over a real serial port. The link owns one open port, sends a framed
// request, and reads until one complete reply frame has decoded, so partial
// reads never reach the driver. A silent peer is a timeout, not malformed data.
#include "io/df1.hpp"
#include "io/df1_link.hpp"
#include "io/serial_io.hpp"

#include <span>
#include <string>
#include <vector>

namespace sc::io {

struct Df1SerialConfig {
    std::string device = "/dev/ttyS0";
    int baud = 19200;
    int timeout_ms = 1000;
    Df1Checksum checksum = Df1Checksum::Bcc;
};

class Df1SerialLink final : public Df1Link {
public:
    explicit Df1SerialLink(Df1SerialConfig config);
    ~Df1SerialLink() override;

    Df1SerialLink(const Df1SerialLink&) = delete;
    Df1SerialLink& operator=(const Df1SerialLink&) = delete;

    bool open(std::string& error) override;
    void close() override;
    bool exchange(std::span<const u8> request, std::vector<u8>& response,
                  std::string& error) override;

    SerialHandle handle() const { return handle_; }

private:
    Df1SerialConfig config_;
    SerialHandle handle_ = kInvalidSerial;
};

}  // namespace sc::io
