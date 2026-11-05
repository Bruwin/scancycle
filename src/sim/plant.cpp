#include "sim/plant.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sc::sim {
namespace {

constexpr f64 kRunningThreshold = 0.95;
constexpr f64 kMovingThreshold = 0.05;
constexpr f64 kMinimumRamp = 1e-6;
constexpr f64 kMetresPerSecondAtFullSpeed = 1.0;
constexpr f64 kEpsilon = 1e-9;

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

u32 Plant::spawn_box(u32 belt) {
    Box box;
    box.belt = belt;
    box.position = 0.0;
    boxes_.push_back(box);
    return static_cast<u32>(boxes_.size()) - 1;
}

u32 Plant::boxes_on(u32 belt) const {
    u32 count = 0;
    for (const Box& box : boxes_) {
        if (box.belt == belt) {
            ++count;
        }
    }
    return count;
}

void Plant::clear_boxes() { boxes_.clear(); }

void Plant::tick(f64 dt_seconds) {
    if (dt_seconds <= 0.0) {
        return;
    }
    ramp_devices(dt_seconds);
    advance_boxes(dt_seconds);
    spawn_due_boxes(dt_seconds);
    update_sensors();
}

void Plant::ramp_devices(f64 dt_seconds) {
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
                // Sensor state is produced by update_sensors, after boxes move.
                break;
        }
    }
}

void Plant::advance_boxes(f64 dt_seconds) {
    for (Device& device : devices_) {
        device.jammed = false;
    }

    // Belts are processed in index order, and boxes within a belt front to back,
    // so a box can never overtake the one ahead of it regardless of framing.
    for (u32 belt_index = 0; belt_index < devices_.size(); ++belt_index) {
        Device& belt = devices_[belt_index];
        if (belt.kind != DeviceKind::Belt) {
            continue;
        }

        std::vector<u32> on_belt;
        for (u32 i = 0; i < boxes_.size(); ++i) {
            if (boxes_[i].belt == belt_index) {
                on_belt.push_back(i);
            }
        }
        std::sort(on_belt.begin(), on_belt.end(),
                  [this](u32 left, u32 right) {
                      return boxes_[left].position > boxes_[right].position;
                  });

        const f64 travel = belt.speed * kMetresPerSecondAtFullSpeed * dt_seconds;

        for (std::size_t i = 0; i < on_belt.size(); ++i) {
            Box& box = boxes_[on_belt[i]];

            f64 limit = belt.length;
            if (i > 0) {
                const Box& ahead = boxes_[on_belt[i - 1]];
                limit = std::min(limit, ahead.position - belt.box_spacing);
            }

            f64 target = box.position + travel;
            target = std::min(target, limit);
            box.position = std::max(box.position, target);

            if (box.position < belt.length - kEpsilon) {
                continue;
            }

            // At the end of the belt: hand off, leave the line, or jam.
            const u32 next_index = belt.next;
            bool moved = false;
            if (next_index != kNoDevice && next_index < devices_.size() &&
                devices_[next_index].kind == DeviceKind::Belt) {
                const f64 next_spacing = devices_[next_index].box_spacing;
                bool room = true;
                for (const Box& other : boxes_) {
                    if (other.belt == next_index && other.position < next_spacing) {
                        room = false;
                        break;
                    }
                }
                if (room) {
                    box.belt = next_index;
                    box.position = 0.0;
                    moved = true;
                }
            } else {
                // No downstream belt: the box has left the line.
                box.belt = kNoDevice;
                moved = true;
            }

            if (moved) {
                ++belt.box_count;
                box.jammed = false;
            } else {
                box.jammed = true;
                belt.jammed = true;
            }
        }
    }

    boxes_.erase(std::remove_if(boxes_.begin(), boxes_.end(),
                                [](const Box& box) { return box.belt == kNoDevice; }),
                 boxes_.end());
}

void Plant::spawn_due_boxes(f64 dt_seconds) {
    for (u32 index = 0; index < devices_.size(); ++index) {
        Device& belt = devices_[index];
        if (belt.kind != DeviceKind::Belt || belt.spawn_period <= 0.0) {
            continue;
        }

        belt.spawn_accumulator += dt_seconds;
        while (belt.spawn_accumulator >= belt.spawn_period) {
            belt.spawn_accumulator -= belt.spawn_period;

            bool clear = true;
            for (const Box& box : boxes_) {
                if (box.belt == index && box.position < belt.box_spacing) {
                    clear = false;
                    break;
                }
            }
            // A blocked infeed does not bank up boxes: the slot is simply lost,
            // which is what a real photo-eye-gated feeder does.
            if (clear) {
                spawn_box(index);
            }
        }
    }
}

void Plant::update_sensors() {
    for (Device& sensor : devices_) {
        if (sensor.kind != DeviceKind::PhotoEye) {
            continue;
        }

        bool sensed = false;
        if (sensor.belt != kNoDevice && sensor.belt < devices_.size()) {
            for (const Box& box : boxes_) {
                if (box.belt == sensor.belt &&
                    std::abs(box.position - sensor.at) <= sensor.window) {
                    sensed = true;
                    break;
                }
            }
        }
        sensor.input = sensed || sensor.blocked;
    }
}

void Plant::reset() {
    boxes_.clear();
    for (Device& device : devices_) {
        device.output = false;
        device.input = false;
        device.speed = 0.0;
        device.distance = 0.0;
        device.box_count = 0;
        device.blocked = false;
        device.jammed = false;
        device.spawn_accumulator = 0.0;
    }
}

Plant make_vertical_slice_plant() {
    Plant plant;

    const u32 infeed = plant.add(DeviceKind::Belt, "infeed_belt", 0, 0, 0);
    plant.device(infeed).length = 3.0;
    plant.device(infeed).spawn_period = 2.0;

    const u32 entry_eye = plant.add(DeviceKind::PhotoEye, "entry_eye", 1, 1, 0);
    plant.device(entry_eye).belt = infeed;
    plant.device(entry_eye).at = 2.0;

    const u32 outfeed = plant.add(DeviceKind::Belt, "outfeed_belt", 3, 0, 0);
    plant.device(outfeed).length = 3.0;
    plant.device(infeed).next = outfeed;

    const u32 run_light = plant.add(DeviceKind::StackLight, "run_light", 4, 2, 0);
    plant.device(run_light).kind = DeviceKind::StackLight;

    return plant;
}

}  // namespace sc::sim
