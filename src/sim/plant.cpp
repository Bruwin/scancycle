#include "sim/plant.hpp"

#include <algorithm>
#include <utility>

namespace sc::sim {
namespace {

constexpr f64 kRunningThreshold = 0.95;
constexpr f64 kMovingThreshold = 0.05;
constexpr f64 kMinimumRamp = 1e-6;

}  // namespace

u32 Plant::add(DeviceKind kind, std::string name, i32 x, i32 y, i32 z) {
    Device device;
    device.kind = kind;
    device.name = std::move(name);
    device.x = x;
    device.y = y;
    device.z = z;
    devices_.push_back(std::move(device));
    return static_cast<u32>(devices_.size()) - 1;
}

const Device& Plant::device(u32 index) const { return devices_[index]; }

Device& Plant::device(u32 index) { return devices_[index]; }

void Plant::set_output(u32 index, bool value) { devices_[index].output = value; }

bool Plant::input(u32 index) const { return devices_[index].input; }

void Plant::set_blocked(u32 index, bool blocked) { devices_[index].blocked = blocked; }

void Plant::tick(f64 dt_seconds) {
    if (dt_seconds <= 0.0) {
        return;
    }

    for (Device& device : devices_) {
        const f64 target = device.output ? 1.0 : 0.0;

        switch (device.kind) {
            case DeviceKind::Motor:
            case DeviceKind::Belt: {
                const f64 ramp = std::max(device.ramp_seconds, kMinimumRamp);
                const f64 step = dt_seconds / ramp;
                if (device.speed < target) {
                    device.speed = std::min(target, device.speed + step);
                } else {
                    device.speed = std::max(target, device.speed - step);
                }

                if (device.kind == DeviceKind::Belt) {
                    device.distance += device.speed * dt_seconds;
                    if (device.box_spacing > kMinimumRamp) {
                        device.box_count =
                            static_cast<i64>(device.distance / device.box_spacing);
                    }
                    device.input = device.speed > kMovingThreshold;
                } else {
                    device.input = device.speed > kRunningThreshold;
                }
                break;
            }
            case DeviceKind::Valve:
            case DeviceKind::StackLight:
                device.speed = target;
                device.input = device.output;
                break;
            case DeviceKind::PhotoEye:
                device.input = device.blocked;
                break;
        }
    }
}

void Plant::reset() {
    for (Device& device : devices_) {
        device.output = false;
        device.input = false;
        device.speed = 0.0;
        device.distance = 0.0;
        device.box_count = 0;
        device.blocked = false;
    }
}

}  // namespace sc::sim
