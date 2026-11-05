#include "io/serial_io.hpp"

#include <cerrno>
#include <cstring>

#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace sc::io {

#if !defined(_WIN32)
namespace {

bool baud_rate(int baud, speed_t& speed) {
    switch (baud) {
        case 1200:
            speed = B1200;
            return true;
        case 2400:
            speed = B2400;
            return true;
        case 4800:
            speed = B4800;
            return true;
        case 9600:
            speed = B9600;
            return true;
        case 19200:
            speed = B19200;
            return true;
        case 38400:
            speed = B38400;
            return true;
#ifdef B57600
        case 57600:
            speed = B57600;
            return true;
#endif
#ifdef B115200
        case 115200:
            speed = B115200;
            return true;
#endif
        default:
            return false;
    }
}

std::string system_error(const char* call) {
    return std::string(call) + " failed: " + std::strerror(errno);
}

}  // namespace

SerialHandle serial_open(std::string_view device, int baud, std::string& error) {
    error.clear();
    speed_t speed = B19200;
    if (!baud_rate(baud, speed)) {
        error = "unsupported serial baud rate " + std::to_string(baud);
        return kInvalidSerial;
    }

    const std::string path(device);
    const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        error = system_error("open");
        return kInvalidSerial;
    }

    termios tty{};
    if (::tcgetattr(fd, &tty) != 0) {
        error = system_error("tcgetattr");
        ::close(fd);
        return kInvalidSerial;
    }

    // Raw 8N1, no handshaking: what DF1 full-duplex expects. Written out rather
    // than cfmakeraw so every flag is visible and the build stays strict-ISO.
    constexpr tcflag_t kIfMask = IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL |
                                 IXON;
    tty.c_iflag &= ~kIfMask;
    tty.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    tty.c_lflag &= ~static_cast<tcflag_t>(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_cflag &= ~static_cast<tcflag_t>(CSIZE | PARENB | CSTOPB);
    tty.c_cflag |= CS8 | CLOCAL | CREAD;
#ifdef CRTSCTS
    tty.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
#endif
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (::cfsetispeed(&tty, speed) != 0 || ::cfsetospeed(&tty, speed) != 0) {
        error = system_error("cfsetspeed");
        ::close(fd);
        return kInvalidSerial;
    }
    if (::tcsetattr(fd, TCSANOW, &tty) != 0) {
        error = system_error("tcsetattr");
        ::close(fd);
        return kInvalidSerial;
    }
    ::tcflush(fd, TCIOFLUSH);
    return fd;
}

bool serial_write(SerialHandle handle, std::span<const u8> data, std::string& error) {
    error.clear();
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t count = ::write(handle, data.data() + sent, data.size() - sent);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = system_error("write");
            return false;
        }
        if (count == 0) {
            error = "serial write made no progress";
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

int serial_read(SerialHandle handle, std::span<u8> buffer, int timeout_ms, bool& timed_out,
                std::string& error) {
    error.clear();
    timed_out = false;

    pollfd entry{};
    entry.fd = handle;
    entry.events = POLLIN;
    int ready = 0;
    do {
        ready = ::poll(&entry, 1, timeout_ms);
    } while (ready < 0 && errno == EINTR);
    if (ready < 0) {
        error = system_error("poll");
        return -1;
    }
    if (ready == 0) {
        timed_out = true;
        return 0;
    }

    const ssize_t count = ::read(handle, buffer.data(), buffer.size());
    if (count < 0) {
        if (errno == EINTR) {
            return 0;
        }
        error = system_error("read");
        return -1;
    }
    if (count == 0) {
        error = "serial port was closed";
        return -1;
    }
    return static_cast<int>(count);
}

void serial_close(SerialHandle handle) {
    if (handle != kInvalidSerial) {
        ::close(handle);
    }
}

#else

SerialHandle serial_open(std::string_view, int, std::string& error) {
    error = "serial DF1 is not implemented on Windows yet";
    return kInvalidSerial;
}

bool serial_write(SerialHandle, std::span<const u8>, std::string& error) {
    error = "serial DF1 is not implemented on Windows yet";
    return false;
}

int serial_read(SerialHandle, std::span<u8>, int, bool&, std::string& error) {
    error = "serial DF1 is not implemented on Windows yet";
    return -1;
}

void serial_close(SerialHandle) {}

#endif

}  // namespace sc::io
