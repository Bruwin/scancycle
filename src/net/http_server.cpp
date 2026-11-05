#include "net/http_server.hpp"

#include "net/http.hpp"

#include <mutex>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace sc::net {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

// A portal request is a request line and maybe a small JSON body. Anything
// larger is not a browser talking to us.
constexpr std::size_t kMaxRequestBytes = 64 * 1024;

void close_socket(SocketHandle socket) {
#if defined(_WIN32)
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}

bool start_sockets(std::string& error) {
#if defined(_WIN32)
    static std::once_flag once;
    static int started = 0;
    std::call_once(once, [] {
        WSADATA data{};
        started = ::WSAStartup(MAKEWORD(2, 2), &data);
    });
    if (started != 0) {
        error = "WSAStartup failed";
        return false;
    }
#else
    (void)error;
#endif
    return true;
}

bool send_all(SocketHandle socket, std::string_view text) {
    std::size_t sent = 0;
    while (sent < text.size()) {
        const char* chunk = text.data() + sent;
        const auto remaining = static_cast<int>(text.size() - sent);
        const int written = static_cast<int>(::send(socket, chunk, remaining, 0));
        if (written <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

}  // namespace

HttpServer::HttpServer(const PortalRouter& router) : router_(&router) {}

HttpServer::~HttpServer() {
    close_listener();
}

void HttpServer::close_listener() {
    if (listener_ >= 0) {
        close_socket(static_cast<SocketHandle>(listener_));
        listener_ = -1;
    }
}

bool HttpServer::listen(u16 port) {
    close_listener();
    if (!start_sockets(error_)) {
        return false;
    }

    const SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket == kInvalidSocket) {
        error_ = "could not create a listening socket";
        return false;
    }

    int reuse = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                 sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(socket, 8) != 0) {
        error_ = "could not bind 127.0.0.1:" + std::to_string(port);
        close_socket(socket);
        return false;
    }

    sockaddr_in bound{};
#if defined(_WIN32)
    int length = static_cast<int>(sizeof(bound));
#else
    socklen_t length = sizeof(bound);
#endif
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &length) == 0) {
        port_ = ntohs(bound.sin_port);
    } else {
        port_ = port;
    }
    listener_ = static_cast<std::intptr_t>(socket);
    return true;
}

bool HttpServer::serve_one() {
    if (listener_ < 0) {
        error_ = "the portal server is not listening";
        return false;
    }

    const SocketHandle client = ::accept(static_cast<SocketHandle>(listener_), nullptr, nullptr);
    if (client == kInvalidSocket) {
        error_ = "accept failed";
        return false;
    }

    std::string raw;
    char buffer[2048];
    while (raw.size() < kMaxRequestBytes) {
        const int got = static_cast<int>(::recv(client, buffer, sizeof(buffer), 0));
        if (got <= 0) {
            break;
        }
        raw.append(buffer, static_cast<std::size_t>(got));
        if (raw.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }

    const ParsedRequest parsed = parse_http_request(raw);
    Response response;
    if (!parsed.ok) {
        response.status = 400;
        response.body = R"({"error":"bad request"})";
    } else {
        response = router_->dispatch(parsed.request);
    }

    send_all(client, format_http_response(response));
    close_socket(client);
    return true;
}

std::string http_round_trip(u16 port, std::string_view raw, std::string& error) {
    error.clear();
    if (!start_sockets(error)) {
        return std::string();
    }

    const SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket == kInvalidSocket) {
        error = "could not create a client socket";
        return std::string();
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        error = "could not connect to 127.0.0.1:" + std::to_string(port);
        close_socket(socket);
        return std::string();
    }

    send_all(socket, raw);

    std::string text;
    char buffer[2048];
    while (true) {
        const int got = static_cast<int>(::recv(socket, buffer, sizeof(buffer), 0));
        if (got <= 0) {
            break;
        }
        text.append(buffer, static_cast<std::size_t>(got));
    }

    close_socket(socket);
    return text;
}

}  // namespace sc::net
