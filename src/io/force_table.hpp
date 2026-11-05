#pragma once
// Values an engineer has pinned by hand. A forced input replaces what the field
// device says after latching, so the program sees the forced value; a forced
// output replaces what the program wrote before flushing, so the field device
// gets it. That is the order a real PLC applies them, and it is why forcing an
// input still leaves the device's own value visible in a trace.
//
// Deliberately tiny and ordered. A force is a debugging act, never a bulk
// operation, so a vector over a handful of entries beats a hash map and keeps
// the listing stable for whoever is reading it.
#include "core/types.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sc::io {

struct Force {
    std::string name;
    bool value = false;
};

class ForceTable {
public:
    // Adds a force, or replaces the value of one already present.
    void set(std::string name, bool value);

    // Returns false when nothing was forced under that name.
    bool clear(std::string_view name);
    void clear_all();

    // False when nothing is forced under that name, in which case `value` is
    // left alone.
    bool find(std::string_view name, bool& value) const;
    bool forced(std::string_view name) const;

    std::size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    const std::vector<Force>& entries() const { return entries_; }

private:
    std::vector<Force> entries_;
};

}  // namespace sc::io
