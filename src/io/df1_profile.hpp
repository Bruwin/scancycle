#pragma once
// Controller profiles record the DF1 defaults an operator would otherwise have
// to look up: station addresses, checksum and serial line settings. They are
// starting points, not guarantees, because the controller's channel
// configuration always wins. The MicroLogix 1000 profile is the reference
// target for this driver: factory Channel 0 is DF1 full-duplex, 19 200 baud,
// 8N1, CRC, controller node 1, host node 0, and its N7 and B3 files are the
// ones this slice can bind.
#include "io/df1_driver.hpp"
#include "io/df1_serial_link.hpp"

#include <string>

namespace sc::io {

struct Df1ControllerProfile {
    const char* name = "";
    u8 controller_address = 1;  // destination: the PLC's DF1 node
    u8 host_address = 0;        // source: this driver's DF1 node
    Df1Checksum checksum = Df1Checksum::Crc16;
    int baud = 19200;
};

inline constexpr Df1ControllerProfile kMicroLogix1000{
    "MicroLogix 1000", 1, 0, Df1Checksum::Crc16, 19200};

// A slave, not a controller: the driver is still the DF1 host.
Df1Config df1_config_for(const Df1ControllerProfile& profile);
Df1SerialConfig df1_serial_config_for(const Df1ControllerProfile& profile, std::string device);

}  // namespace sc::io
