#include "io/force_table.hpp"

#include <algorithm>
#include <utility>

namespace sc::io {

void ForceTable::set(std::string name, bool value) {
    for (Force& force : entries_) {
        if (force.name == name) {
            force.value = value;
            return;
        }
    }
    entries_.push_back(Force{std::move(name), value});
}

bool ForceTable::clear(std::string_view name) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [name](const Force& force) { return force.name == name; });
    if (it == entries_.end()) {
        return false;
    }
    entries_.erase(it);
    return true;
}

void ForceTable::clear_all() { entries_.clear(); }

bool ForceTable::find(std::string_view name, bool& value) const {
    for (const Force& force : entries_) {
        if (force.name == name) {
            value = force.value;
            return true;
        }
    }
    return false;
}

bool ForceTable::forced(std::string_view name) const {
    bool ignored = false;
    return find(name, ignored);
}

}  // namespace sc::io
