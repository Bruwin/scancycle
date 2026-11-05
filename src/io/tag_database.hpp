#pragma once
// Maps PLC symbol names onto plant devices. The database is built once at load
// time; binding is name-based, but lookups after binding are index-based so the
// per-scan cost does not depend on the number of tags.
#include "core/types.hpp"
#include "sim/plant.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sc::io {

enum class TagDirection : u8 { Input = 0, Output };

struct Tag {
    std::string name;
    TagDirection direction = TagDirection::Input;
    u32 device = 0;
};

class TagDatabase {
public:
    u32 bind(std::string name, TagDirection direction, u32 device);

    i32 find(std::string_view name) const;
    const Tag& tag(u32 index) const { return tags_[index]; }
    std::size_t size() const { return tags_.size(); }
    const std::vector<Tag>& tags() const { return tags_; }

    bool read_bool(u32 index, const sim::Plant& plant, bool& out) const;
    bool write_bool(u32 index, bool value, sim::Plant& plant) const;

private:
    std::vector<Tag> tags_;
    std::unordered_map<std::string, u32> lookup_;
};

}  // namespace sc::io
