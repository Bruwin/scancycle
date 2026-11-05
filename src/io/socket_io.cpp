#include "io/socket_io.hpp"

#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace sc::io {
namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kNativeInvalid = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kNativeInvalid = -1;
#endif

NativeSocket native(SocketHandle handle) {
    return static_cast<NativeSocket>(handle);
}

SocketHandle handle(NativeSocket socket) {
    return static_cast<SocketHandle>(socket);
}

void close_native(NativeSocket socket) {
#if defined(_WIN32)
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}

std::string system_error() {
#if defined(_WIN32)
    return "socket error " + std::to_string(::WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

void set_timeouts(NativeSocket socket, int timeout_ms) {
    if (timeout_ms <= 0) {
        timeout_ms = 1000;
    }
#if defined(_WIN32)
    const DWORD timeout = static_cast<DWORD>(timeout_ms);
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                 sizeof(timeout));
    ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
                 sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

void disable_sigpipe(NativeSocket socket) {
#if defined(SO_NOSIGPIPE)
    int enabled = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&enabled),
                 sizeof(enabled));
#else
    (void)socket;
#endif
}

}  // namespace

bool socket_startup(std::string& error) {
#if defined(_WIN32)
    static std::once_flag once;
    static int result = 0;
    std::call_once(once, [] {
        WSADATA data{};
        result = ::WSAStartup(MAKEWORD(2, 2), &data);
    });
    if (result != 0) {
        error = "WSAStartup failed";
        return false;
    }
#else
    (void)error;
#endif
    return true;
}

SocketHandle connect_tcp(std::string_view host, u16 port, int timeout_ms,
                         std::string& error) {
    error.clear();
    if (!socket_startup(error)) {
        return kInvalidSocket;
    }
    const std::string host_text(host);
    const std::string port_text = std::to_string(port);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* results = nullptr;
    if (::getaddrinfo(host_text.c_str(), port_text.c_str(), &hints, &results) != 0) {
        error = "could not resolve '" + host_text + "'";
        return kInvalidSocket;
    }

    NativeSocket connected = kNativeInvalid;
    for (addrinfo* candidate = results; candidate != nullptr; candidate = candidate->ai_next) {
        const NativeSocket socket =
            ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (socket == kNativeInvalid) {
            continue;
        }
        set_timeouts(socket, timeout_ms);
        disable_sigpipe(socket);
        if (::connect(socket, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0) {
            connected = socket;
            break;
        }
        close_native(socket);
    }
    ::freeaddrinfo(results);

    if (connected == kNativeInvalid) {
        error = "could not connect to " + host_text + ":" + port_text;
        return kInvalidSocket;
    }
    return handle(connected);
}

SocketHandle listen_loopback(u16 port, u16& bound_port, std::string& error) {
    error.clear();
    if (!socket_startup(error)) {
        return kInvalidSocket;
    }

    const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket == kNativeInvalid) {
        error = "could not create the listener: " + system_error();
        return kInvalidSocket;
    }

    int reuse = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                 sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(socket, 4) != 0) {
        error = "could not bind 127.0.0.1:" + std::to_string(port);
        close_native(socket);
        return kInvalidSocket;
    }

    sockaddr_in bound{};
#if defined(_WIN32)
    int length = static_cast<int>(sizeof(bound));
#else
    socklen_t length = sizeof(bound);
#endif
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &length) == 0) {
        bound_port = ntohs(bound.sin_port);
    } else {
        bound_port = port;
    }
    return handle(socket);
}

SocketHandle accept_tcp(SocketHandle listener, int timeout_ms, bool& timed_out,
                        std::string& error) {
    timed_out = false;
    error.clear();
    if (listener == kInvalidSocket) {
        error = "listener is not open";
        return kInvalidSocket;
    }
    if (timeout_ms <= 0) {
        timeout_ms = 1000;
    }

    fd_set read_set;
    FD_ZERO(&read_set);
    const NativeSocket native_listener = native(listener);
    FD_SET(native_listener, &read_set);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
#if defined(_WIN32)
    const int ready = ::select(0, &read_set, nullptr, nullptr, &timeout);
#else
    const int ready =
        ::select(static_cast<int>(native_listener) + 1, &read_set, nullptr, nullptr, &timeout);
#endif
    if (ready == 0) {
        timed_out = true;
        return kInvalidSocket;
    }
    if (ready < 0) {
        error = "accept wait failed: " + system_error();
        return kInvalidSocket;
    }

    const NativeSocket client = ::accept(native_listener, nullptr, nullptr);
    if (client == kNativeInvalid) {
        error = "accept failed: " + system_error();
        return kInvalidSocket;
    }
    set_timeouts(client, timeout_ms);
    disable_sigpipe(client);
    return handle(client);
}

bool socket_send(SocketHandle socket, std::span<const u8> data, std::string& error) {
    error.clear();
    if (data.empty()) {
        return true;
    }
    std::size_t sent = 0;
    while (sent < data.size()) {
        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags = MSG_NOSIGNAL;
#endif
        const int written =
            static_cast<int>(::send(native(socket),
                                    reinterpret_cast<const char*>(data.data() + sent),
                                    static_cast<int>(data.size() - sent), flags));
        if (written <= 0) {
            error = "socket send failed: " + system_error();
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

bool socket_receive(SocketHandle socket, std::span<u8> data, std::string& error) {
    error.clear();
    std::size_t received = 0;
    while (received < data.size()) {
        const int got = static_cast<int>(
            ::recv(native(socket), reinterpret_cast<char*>(data.data() + received),
                   static_cast<int>(data.size() - received), 0));
        if (got == 0) {
            error = "connection closed";
            return false;
        }
        if (got < 0) {
#if !defined(_WIN32)
            if (errno == EINTR) {
                continue;
            }
#endif
            error = "socket receive failed: " + system_error();
            return false;
        }
        received += static_cast<std::size_t>(got);
    }
    return true;
}

void socket_shutdown(SocketHandle socket) {
    if (socket == kInvalidSocket) {
        return;
    }
#if defined(_WIN32)
    ::shutdown(native(socket), SD_BOTH);
#else
    ::shutdown(native(socket), SHUT_RDWR);
#endif
}

void socket_close(SocketHandle socket) {
    if (socket == kInvalidSocket) {
        return;
    }
    close_native(native(socket));
}

}  // namespace sc::io
