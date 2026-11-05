#pragma once
// The physical plant. Devices are deliberately dumb: they hold state, they ramp
// towards a command, and they expose one boolean feedback. All plant timing is
// driven by the tick argument, never by a clock.
#include "core/types.hpp"

#include <string>
#include <vector>

namespace sc::sim {

enum class DeviceKind : u8 { Motor = 0, Belt, PhotoEye, Valve, StackLight };

struct Device {
    DeviceKind kind = DeviceKind::Motor;
    std::string name;

    bool output = false;  // command written by the PLC
    bool input = false;   // feedback read by the PLC

    f64 speed = 0.0;         // normalised 0..1
    f64 ramp_seconds = 0.5;  // time from stopped to full speed
    f64 distance = 0.0;      // belt travel, in metres
    f64 box_spacing = 1.0;   // metres between boxes
    i64 box_count = 0;
    bool blocked = false;    // photo-eye occlusion

    i32 x = 0;
    i32 y = 0;
    i32 z = 0;
};

class Plant {
public:
    u32 add(DeviceKind kind, std::string name, i32 x = 0, i32 y = 0, i32 z = 0);

    u32 count() const { return static_cast<u32>(devices_.size()); }
    const Device& device(u32 index) const;
    Device& device(u32 index);

    void set_output(u32 index, bool value);
    bool input(u32 index) const;
    void set_blocked(u32 index, bool blocked);

    // Advances every device by dt seconds. dt must be positive; a zero or
    // negative step is ignored rather than rewinding the plant.
    void tick(f64 dt_seconds);

    void reset();

    const std::vector<Device>& devices() const { return devices_; }

private:
    std::vector<Device> devices_;
};

}  // namespace sc::sim
