#pragma once
// A small loopback Modbus/TCP slave: one connection, holding registers, the two
// function codes the driver uses. It exists so the live transport can be tested
// without hardware and so a classroom can exercise a driver against a known
// register image. It is not a general-purpose Modbus server.
#include "core/types.hpp"
#include "io/socket_io.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace sc::io {

class ModbusTcpSlave {
public:
    ModbusTcpSlave();
    ~ModbusTcpSlave();

    ModbusTcpSlave(const ModbusTcpSlave&) = delete;
    ModbusTcpSlave& operator=(const ModbusTcpSlave&) = delete;

    // Binds 127.0.0.1:port (0 asks the kernel). Starts no thread: the caller
    // decides whether serve_one() runs on this thread or another.
    bool listen(u16 port);
    u16 port() const { return port_; }

    // Accepts one client and serves it until it disconnects. A timeout while
    // waiting lets stop() take effect without waiting for a client.
    bool serve_one();
    void stop();

    void set_register(u16 address, u16 value);
    u16 register_value(u16 address) const;

    const std::string& error() const { return error_; }

private:
    bool read_frame(SocketHandle client, std::vector<u8>& frame);
    bool handle_frame(const std::vector<u8>& request, std::vector<u8>& response);

    std::atomic<SocketHandle> listener_{kInvalidSocket};
    std::atomic<SocketHandle> client_{kInvalidSocket};
    std::atomic<bool> running_{false};
    u16 port_ = 0;
    mutable std::mutex registers_mutex_;
    std::vector<u16> registers_;
    std::string error_;
};

}  // namespace sc::io
