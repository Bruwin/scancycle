#pragma once
// POSIX serial-port primitives for the DF1 link. The protocol never sees an fd:
// raw 8N1 set-up, an exact write, and one bounded read live here so the termios
// details can be read in one place. Windows has no implementation yet and
// reports that as a value rather than failing at link time.
#include "core/types.hpp"

#include <span>
#include <string>
#include <string_view>

namespace sc::io {

using SerialHandle = int;
inline constexpr SerialHandle kInvalidSerial = -1;

SerialHandle serial_open(std::string_view device, int baud, std::string& error);
bool serial_write(SerialHandle handle, std::span<const u8> data, std::string& error);

// Reads at least one byte, waiting at most timeout_ms. Returns the byte count,
// 0 on timeout with `timed_out` set, or -1 when the port failed or closed.
int serial_read(SerialHandle handle, std::span<u8> buffer, int timeout_ms, bool& timed_out,
                std::string& error);

void serial_close(SerialHandle handle);

}  // namespace sc::io
