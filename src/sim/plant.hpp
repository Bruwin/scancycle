#pragma once
// The physical plant. Devices are deliberately dumb: they hold state, they ramp
// towards a command, and they expose one boolean feedback. All plant timing is
// driven by the tick argument, never by a clock.
//
// Boxes are the unit of material flow. They are owned by the plant rather than
// by a belt, so handing one to the next belt is a change of owner, not a copy.
#include "core/types.hpp"

#include <string>
#include <vector>

namespace sc::sim {

inline constexpr u32 kNoDevice = 0xFFFF'FFFFu;

enum class DeviceKind : u8 { Motor = 0, Belt, PhotoEye, Valve, StackLight, Machine };

struct Box {
    u32 belt = kNoDevice;  // the belt or machine holding it; kNoDevice once gone
    f64 position = 0.0;    // metres from the infeed end of its belt
    bool jammed = false;
};

struct Device {
    DeviceKind kind = DeviceKind::Motor;
    std::string name;

    bool output = false;  // command written by the PLC
    bool input = false;   // feedback read by the PLC

    f64 speed = 0.0;         // normalised 0..1
    f64 ramp_seconds = 0.5;  // time from stopped to full speed
    f64 distance = 0.0;      // total belt surface travel, metres
    f64 box_spacing = 0.6;   // minimum gap between boxes, metres
    i64 box_count = 0;       // boxes that have left this belt
    bool blocked = false;    // manual occlusion override (lessons, tests)
    bool jammed = false;     // a box could not advance

    // Belt geometry and routing.
    f64 length = 4.0;        // metres
    u32 next = kNoDevice;    // downstream belt, or a machine's output belt
    f64 spawn_period = 0.0;  // seconds between infeed boxes; 0 disables spawning
    f64 spawn_accumulator = 0.0;

    // Photo-eye mounting: which belt, and where along it.
    u32 belt = kNoDevice;
    f64 at = 0.0;
    f64 window = 0.2;  // half-width of the sensing band, metres

    // Machine: pulls one box from `infeed`, holds it for a cycle, releases it.
    u32 infeed = kNoDevice;
    f64 cycle_seconds = 2.0;
    f64 progress = 0.0;  // seconds into the current cycle
    i64 done_count = 0;  // boxes processed
    bool busy = false;

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

    // Places a box at the infeed end of `belt`. Returns the new box index.
    u32 spawn_box(u32 belt);
    u32 box_count() const { return static_cast<u32>(boxes_.size()); }
    const Box& box(u32 index) const { return boxes_[index]; }
    u32 boxes_on(u32 belt) const;
    void clear_boxes();

    // Advances every device by dt seconds. dt must be positive; a zero or
    // negative step is ignored rather than rewinding the plant.
    void tick(f64 dt_seconds);

    void reset();

    const std::vector<Device>& devices() const { return devices_; }
    const std::vector<Box>& boxes() const { return boxes_; }

private:
    void ramp_devices(f64 dt_seconds);
    void advance_boxes(f64 dt_seconds);
    void update_machines(f64 dt_seconds);
    void spawn_due_boxes(f64 dt_seconds);
    void update_sensors();
    void erase_left_line();

    std::vector<Device> devices_;
    std::vector<Box> boxes_;
};

// The M3 vertical slice: infeed belt, entry photo-eye, a machine, an outfeed
// belt and a stack light. Enough to program a real interlock against.
Plant make_vertical_slice_plant();

}  // namespace sc::sim
