#include "io/modbus_slave.hpp"

#include "io/modbus.hpp"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace sc::io {
namespace {

u16 read_u16(const std::vector<u8>& frame, std::size_t at) {
    return static_cast<u16>((static_cast<u16>(frame[at]) << 8) | static_cast<u16>(frame[at + 1]));
}

}  // namespace

ModbusTcpSlave::ModbusTcpSlave() : registers_(65536, 0) {}

ModbusTcpSlave::~ModbusTcpSlave() { stop(); }

bool ModbusTcpSlave::listen(u16 port) {
    stop();
    error_.clear();
    if (!socket_startup(error_)) {
        return false;
    }
    SocketHandle listener = listen_loopback(port, port_, error_);
    if (listener == kInvalidSocket) {
        return false;
    }
    listener_.store(listener);
    running_.store(true);
    return true;
}

bool ModbusTcpSlave::read_frame(SocketHandle client, std::vector<u8>& frame) {
    std::array<u8, kMbapHeaderBytes> header{};
    if (!socket_receive(client, header, error_)) {
        return false;
    }
    const u16 length =
        static_cast<u16>((static_cast<u16>(header[4]) << 8) | static_cast<u16>(header[5]));
    if (length < 2 || length > 254) {
        error_ = "request has an invalid MBAP length";
        return false;
    }

    frame.assign(header.begin(), header.end());
    frame.resize(6 + length);
    return socket_receive(client, std::span<u8>(frame).subspan(kMbapHeaderBytes), error_);
}

bool ModbusTcpSlave::handle_frame(const std::vector<u8>& request,
                                  std::vector<u8>& response) {
    if (request.size() < 8) {
        error_ = "request is too short";
        return false;
    }
    if (read_u16(request, 2) != kModbusProtocolId ||
        read_u16(request, 4) != request.size() - 6) {
        error_ = "request has an invalid MBAP header";
        return false;
    }

    const u16 transaction = read_u16(request, 0);
    const u8 unit = request[6];
    const u8 function = request[7];

    if (function == kFuncReadHoldingRegisters) {
        if (request.size() != 12) {
            response = build_exception_response(transaction, unit, function, 3);
            return true;
        }
        const u16 address = read_u16(request, 8);
        const u16 count = read_u16(request, 10);
        if (count == 0 || count > 125) {
            response = build_exception_response(transaction, unit, function, 3);
            return true;
        }
        if (static_cast<u32>(address) + count > 65536U) {
            response = build_exception_response(transaction, unit, function, 2);
            return true;
        }

        std::vector<u16> registers;
        registers.reserve(count);
        {
            std::lock_guard<std::mutex> lock(registers_mutex_);
            for (u32 offset = 0; offset < count; ++offset) {
                registers.push_back(registers_[address + offset]);
            }
        }
        response = build_read_response(transaction, unit, registers);
        return true;
    }

    if (function == kFuncWriteSingleRegister) {
        if (request.size() != 12) {
            response = build_exception_response(transaction, unit, function, 3);
            return true;
        }
        const u16 address = read_u16(request, 8);
        const u16 value = read_u16(request, 10);
        {
            std::lock_guard<std::mutex> lock(registers_mutex_);
            registers_[address] = value;
        }
        response = request;
        return true;
    }

    response = build_exception_response(transaction, unit, function, 1);
    return true;
}

bool ModbusTcpSlave::serve_one() {
    error_.clear();
    const SocketHandle listener = listener_.load();
    if (listener == kInvalidSocket || !running_.load()) {
        error_ = "slave is not listening";
        return false;
    }

    while (running_.load()) {
        bool timed_out = false;
        std::string accept_error;
        const SocketHandle client = accept_tcp(listener, 100, timed_out, accept_error);
        if (client == kInvalidSocket) {
            if (!running_.load()) {
                return false;
            }
            if (timed_out) {
                continue;
            }
            error_ = accept_error;
            return false;
        }

        client_.store(client);
        bool healthy = true;
        while (running_.load()) {
            std::vector<u8> request;
            if (!read_frame(client, request)) {
                break;
            }
            std::vector<u8> response;
            if (!handle_frame(request, response) ||
                !socket_send(client, response, error_)) {
                healthy = false;
                break;
            }
        }
        client_.store(kInvalidSocket);
        socket_shutdown(client);
        socket_close(client);
        return healthy;
    }
    return false;
}

void ModbusTcpSlave::stop() {
    running_.store(false);
    const SocketHandle listener = listener_.exchange(kInvalidSocket);
    if (listener != kInvalidSocket) {
        socket_shutdown(listener);
        socket_close(listener);
    }
    const SocketHandle client = client_.exchange(kInvalidSocket);
    if (client != kInvalidSocket) {
        // serve_one() owns the close; shutting the socket down is enough to
        // wake a blocking receive without risking a second close of the fd.
        socket_shutdown(client);
    }
}

void ModbusTcpSlave::set_register(u16 address, u16 value) {
    std::lock_guard<std::mutex> lock(registers_mutex_);
    registers_[address] = value;
}

u16 ModbusTcpSlave::register_value(u16 address) const {
    std::lock_guard<std::mutex> lock(registers_mutex_);
    return registers_[address];
}

}  // namespace sc::io
