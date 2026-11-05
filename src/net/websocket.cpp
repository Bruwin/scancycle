#include "net/websocket.hpp"

#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
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

constexpr std::size_t kMaxHandshakeBytes = 8192;
constexpr std::size_t kMaxClients = 64;
constexpr std::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::uint32_t octet(const std::string& text, std::size_t index) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(text[index]));
}

bool equals_ci(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

// Headers are deliberately read as views into the buffered handshake; the
// caller finishes with the buffer before it goes out of scope.
std::string_view header_value(std::string_view raw, std::string_view name) {
    const std::size_t request_end = raw.find("\r\n");
    if (request_end == std::string_view::npos) {
        return {};
    }
    std::size_t pos = request_end + 2;
    while (pos < raw.size()) {
        const std::size_t end = raw.find("\r\n", pos);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string_view line = raw.substr(pos, end - pos);
        pos = end + 2;
        if (line.empty()) {
            break;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        if (equals_ci(trim(line.substr(0, colon)), name)) {
            return trim(line.substr(colon + 1));
        }
    }
    return {};
}

bool header_has_token(std::string_view raw, std::string_view name, std::string_view token) {
    std::string_view value = header_value(raw, name);
    while (!value.empty()) {
        const std::size_t comma = value.find(',');
        const std::string_view piece = trim(value.substr(0, comma));
        if (equals_ci(piece, token)) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        value.remove_prefix(comma + 1);
    }
    return false;
}

std::uint32_t rotate_left(std::uint32_t value, unsigned bits) {
    return (value << bits) | (value >> (32 - bits));
}

std::string sha1(std::string_view data) {
    std::array<std::uint32_t, 5> hash = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u,
                                         0xC3D2E1F0u};
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8;
    std::string message(data);
    message.push_back(static_cast<char>(0x80));
    while (message.size() % 64 != 56) {
        message.push_back('\0');
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        message.push_back(static_cast<char>((bit_length >> shift) & 0xFF));
    }

    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::array<std::uint32_t, 80> words{};
        for (int i = 0; i < 16; ++i) {
            const std::size_t start = offset + static_cast<std::size_t>(i) * 4;
            words[static_cast<std::size_t>(i)] = (octet(message, start) << 24) |
                                                  (octet(message, start + 1) << 16) |
                                                  (octet(message, start + 2) << 8) |
                                                  octet(message, start + 3);
        }
        for (std::size_t i = 16; i < 80; ++i) {
            words[i] = rotate_left(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
        }

        std::uint32_t a = hash[0];
        std::uint32_t b = hash[1];
        std::uint32_t c = hash[2];
        std::uint32_t d = hash[3];
        std::uint32_t e = hash[4];
        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t function = 0;
            std::uint32_t constant = 0;
            if (i < 20) {
                function = (b & c) | ((~b) & d);
                constant = 0x5A827999u;
            } else if (i < 40) {
                function = b ^ c ^ d;
                constant = 0x6ED9EBA1u;
            } else if (i < 60) {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8F1BBCDCu;
            } else {
                function = b ^ c ^ d;
                constant = 0xCA62C1D6u;
            }
            const std::uint32_t temp =
                rotate_left(a, 5) + function + e + constant + words[i];
            e = d;
            d = c;
            c = rotate_left(b, 30);
            b = a;
            a = temp;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
    }

    std::string digest;
    digest.reserve(20);
    for (const std::uint32_t word : hash) {
        digest.push_back(static_cast<char>((word >> 24) & 0xFF));
        digest.push_back(static_cast<char>((word >> 16) & 0xFF));
        digest.push_back(static_cast<char>((word >> 8) & 0xFF));
        digest.push_back(static_cast<char>(word & 0xFF));
    }
    return digest;
}

std::string base64_encode(std::string_view data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(static_cast<unsigned char>(data[i])) << 16) |
            (i + 1 < data.size()
                 ? static_cast<std::uint32_t>(static_cast<unsigned char>(data[i + 1])) << 8
                 : 0) |
            (i + 2 < data.size()
                 ? static_cast<std::uint32_t>(static_cast<unsigned char>(data[i + 2]))
                 : 0);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(i + 1 < data.size() ? kAlphabet[(chunk >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < data.size() ? kAlphabet[chunk & 0x3F] : '=');
    }
    return out;
}

void close_socket(SocketHandle socket) {
#if defined(_WIN32)
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}

void shutdown_socket(SocketHandle socket) {
#if defined(_WIN32)
    ::shutdown(socket, SD_BOTH);
#else
    ::shutdown(socket, SHUT_RDWR);
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

void disable_sigpipe(SocketHandle socket) {
#if defined(SO_NOSIGPIPE)
    int enabled = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&enabled),
                 sizeof(enabled));
#else
    (void)socket;
#endif
}

void set_receive_timeout(SocketHandle socket) {
#if defined(_WIN32)
    DWORD timeout = 2000;
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                 sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = 2;
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

bool send_all(SocketHandle socket, std::string_view text) {
    std::size_t sent = 0;
    while (sent < text.size()) {
        const char* chunk = text.data() + sent;
        const int remaining = static_cast<int>(text.size() - sent);
        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags = MSG_NOSIGNAL;
#endif
        const int written = static_cast<int>(::send(socket, chunk, remaining, flags));
        if (written <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

bool read_handshake(SocketHandle socket, std::string& raw) {
    char buffer[1024];
    while (raw.size() < kMaxHandshakeBytes) {
        const int got = static_cast<int>(::recv(socket, buffer, sizeof(buffer), 0));
        if (got <= 0) {
            return false;
        }
        raw.append(buffer, static_cast<std::size_t>(got));
        if (raw.find("\r\n\r\n") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool is_live_request(std::string_view raw) {
    const std::size_t request_end = raw.find("\r\n");
    if (request_end == std::string_view::npos) {
        return false;
    }
    const std::string_view line = raw.substr(0, request_end);
    if (line.rfind("GET ", 0) != 0) {
        return false;
    }
    const std::size_t target_start = 4;
    const std::size_t target_end = line.find(' ', target_start);
    if (target_end == std::string_view::npos) {
        return false;
    }
    const std::string_view target = line.substr(target_start, target_end - target_start);
    const bool live_path = target == "/live" || target.rfind("/live?", 0) == 0;
    return live_path && header_has_token(raw, "Upgrade", "websocket") &&
           header_has_token(raw, "Connection", "upgrade") &&
           header_value(raw, "Sec-WebSocket-Version") == "13" &&
           !header_value(raw, "Sec-WebSocket-Key").empty();
}

std::string handshake_response(std::string_view raw) {
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: ";
    response += websocket_accept_key(header_value(raw, "Sec-WebSocket-Key"));
    response += "\r\n\r\n";
    return response;
}

constexpr std::string_view kBadHandshake =
    "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

}  // namespace

std::string websocket_accept_key(std::string_view key) {
    std::string input(key);
    input += kWebSocketGuid;
    return base64_encode(sha1(input));
}

std::string websocket_text_frame(std::string_view text) {
    std::string frame;
    frame.push_back(static_cast<char>(0x81));  // FIN + text
    const std::size_t size = text.size();
    if (size < 126) {
        frame.push_back(static_cast<char>(size));
    } else if (size <= 0xFFFF) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((size >> 8) & 0xFF));
        frame.push_back(static_cast<char>(size & 0xFF));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<char>((static_cast<std::uint64_t>(size) >> shift) & 0xFF));
        }
    }
    frame.append(text);
    return frame;
}

struct WebSocketServer::Impl {
    std::atomic<std::intptr_t> listener{-1};
    std::atomic<bool> running{false};
    std::thread accept_thread;
    mutable std::mutex clients_mutex;
    std::vector<SocketHandle> clients;
    u16 port = 0;
    std::string error;

    void accept_loop();
    void stop();
};

void WebSocketServer::Impl::accept_loop() {
    while (running.load()) {
        const std::intptr_t listener_value = listener.load();
        if (listener_value < 0) {
            break;
        }
        const SocketHandle accept_socket = static_cast<SocketHandle>(listener_value);
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(accept_socket, &read_set);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
#if defined(_WIN32)
        const int ready = ::select(0, &read_set, nullptr, nullptr, &timeout);
#else
        const int ready =
            ::select(static_cast<int>(accept_socket) + 1, &read_set, nullptr, nullptr, &timeout);
#endif
        if (!running.load()) {
            break;
        }
        if (ready <= 0) {
            continue;
        }

        const SocketHandle client = ::accept(accept_socket, nullptr, nullptr);
        if (client == kInvalidSocket) {
            continue;
        }
        disable_sigpipe(client);
        set_receive_timeout(client);

        std::string raw;
        if (!read_handshake(client, raw) || !is_live_request(raw)) {
            send_all(client, kBadHandshake);
            close_socket(client);
            continue;
        }
        if (!running.load()) {
            close_socket(client);
            break;
        }
        if (!send_all(client, handshake_response(raw))) {
            close_socket(client);
            continue;
        }

        std::lock_guard<std::mutex> lock(clients_mutex);
        if (clients.size() >= kMaxClients) {
            close_socket(client);
            continue;
        }
        clients.push_back(client);
    }
}

void WebSocketServer::Impl::stop() {
    running.store(false);
    const std::intptr_t listener_value = listener.exchange(-1);
    if (listener_value >= 0) {
        close_socket(static_cast<SocketHandle>(listener_value));
    }
    if (accept_thread.joinable()) {
        accept_thread.join();
    }

    std::lock_guard<std::mutex> lock(clients_mutex);
    for (const SocketHandle client : clients) {
        shutdown_socket(client);
        close_socket(client);
    }
    clients.clear();
}

WebSocketServer::WebSocketServer() : impl_(std::make_unique<Impl>()) {}

WebSocketServer::~WebSocketServer() {
    if (impl_) {
        impl_->stop();
    }
}

bool WebSocketServer::listen(u16 port) {
    impl_->stop();
    impl_->error.clear();
    if (!start_sockets(impl_->error)) {
        return false;
    }

    const SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket == kInvalidSocket) {
        impl_->error = "could not create a WebSocket socket";
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
        impl_->error = "could not bind 127.0.0.1:" + std::to_string(port);
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
        impl_->port = ntohs(bound.sin_port);
    } else {
        impl_->port = port;
    }

    impl_->listener.store(static_cast<std::intptr_t>(socket));
    impl_->running.store(true);
    impl_->accept_thread = std::thread([this] { impl_->accept_loop(); });
    return true;
}

void WebSocketServer::stop() { impl_->stop(); }

u16 WebSocketServer::port() const { return impl_->port; }

std::size_t WebSocketServer::client_count() const {
    std::lock_guard<std::mutex> lock(impl_->clients_mutex);
    return impl_->clients.size();
}

void WebSocketServer::broadcast(std::string_view text) {
    const std::string frame = websocket_text_frame(text);
    std::lock_guard<std::mutex> lock(impl_->clients_mutex);
    for (auto it = impl_->clients.begin(); it != impl_->clients.end();) {
        if (!send_all(*it, frame)) {
            shutdown_socket(*it);
            close_socket(*it);
            it = impl_->clients.erase(it);
        } else {
            ++it;
        }
    }
}

const std::string& WebSocketServer::error() const { return impl_->error; }

}  // namespace sc::net
