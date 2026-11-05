#include "io/tag_database.hpp"

#include <utility>

namespace sc::io {

u32 TagDatabase::bind(std::string name, TagDirection direction, u32 device) {
    const u32 index = static_cast<u32>(tags_.size());
    lookup_.emplace(name, index);
    tags_.push_back(Tag{std::move(name), direction, device});
    return index;
}

i32 TagDatabase::find(std::string_view name) const {
    const auto it = lookup_.find(std::string(name));
    if (it == lookup_.end()) {
        return -1;
    }
    return static_cast<i32>(it->second);
}

bool TagDatabase::read_bool(u32 index, const sim::Plant& plant, bool& out) const {
    if (index >= tags_.size()) {
        return false;
    }
    const Tag& entry = tags_[index];
    if (entry.direction != TagDirection::Input) {
        return false;
    }
    out = plant.input(entry.device);
    return true;
}

bool TagDatabase::write_bool(u32 index, bool value, sim::Plant& plant) const {
    if (index >= tags_.size()) {
        return false;
    }
    const Tag& entry = tags_[index];
    if (entry.direction != TagDirection::Output) {
        return false;
    }
    plant.set_output(entry.device, value);
    return true;
}

}  // namespace sc::io
