#include "io/df1_profile.hpp"

#include <utility>

namespace sc::io {

Df1Config df1_config_for(const Df1ControllerProfile& profile) {
    Df1Config config;
    config.station = profile.host_address;
    config.destination = profile.controller_address;
    config.checksum = profile.checksum;
    return config;
}

Df1SerialConfig df1_serial_config_for(const Df1ControllerProfile& profile,
                                      std::string device) {
    Df1SerialConfig config;
    config.device = std::move(device);
    config.baud = profile.baud;
    config.checksum = profile.checksum;
    return config;
}

}  // namespace sc::io
