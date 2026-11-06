#include "plc/netlist_edit.hpp"

#include <utility>

namespace sc::plc {
namespace {

struct Located {
    NetNode* node = nullptr;
    NetNode* parent = nullptr;
    std::size_t index = 0;
};

bool locate(NetPtr& root, const NetPath& path, Located& out) {
    if (!root) {
        return false;
    }
    if (path.empty()) {
        out = {root.get(), nullptr, 0};
        return true;
    }

    NetNode* parent = root.get();
    for (std::size_t depth = 0; depth < path.size(); ++depth) {
        if (parent == nullptr || parent->kind == NetKind::Contact ||
            parent->kind == NetKind::Always) {
            return false;
        }
        const std::size_t index = path[depth];
        if (index >= parent->operands.size() || !parent->operands[index]) {
            return false;
        }
        if (depth + 1 == path.size()) {
            out = {parent->operands[index].get(), parent, index};
            return true;
        }
        parent = parent->operands[index].get();
    }
    return false;
}

NetPtr make_contact(std::string name, bool negated) {
    auto node = std::make_unique<NetNode>();
    node->kind = NetKind::Contact;
    node->var = std::move(name);
    node->negated = negated;
    return node;
}

NetPtr make_junction(NetKind kind, NetPtr first, NetPtr second) {
    auto node = std::make_unique<NetNode>();
    node->kind = kind;
    node->operands.push_back(std::move(first));
    node->operands.push_back(std::move(second));
    return node;
}

NetPtr& slot(NetPtr& root, const Located& at) {
    return at.parent == nullptr ? root : at.parent->operands[at.index];
}

bool check_name(const std::string& name, NetEditResult& result) {
    if (name.empty()) {
        result = {false, "a contact needs a tag name"};
        return false;
    }
    return true;
}

NetEditResult refused(std::string error) { return {false, std::move(error)}; }

}  // namespace

const NetNode* find_node(const Rung& rung, const NetPath& path) {
    if (!rung.condition) {
        return nullptr;
    }
    if (path.empty()) {
        return rung.condition.get();
    }
    const NetNode* node = rung.condition.get();
    for (const std::size_t index : path) {
        if (node == nullptr || node->kind == NetKind::Contact ||
            node->kind == NetKind::Always || index >= node->operands.size()) {
            return nullptr;
        }
        node = node->operands[index].get();
    }
    return node;
}

NetNode* find_node(Rung& rung, const NetPath& path) {
    Located located;
    if (!locate(rung.condition, path, located)) {
        return nullptr;
    }
    return located.node;
}

NetEditResult set_coil(Rung& rung, std::size_t index, std::string name, bool negated) {
    NetEditResult result;
    if (!check_name(name, result)) {
        return result;
    }
    if (index >= rung.coils.size()) {
        return refused("there is no coil at that index");
    }
    rung.coils[index] = Coil{std::move(name), negated};
    return {true, {}};
}

NetEditResult add_coil(Rung& rung, std::string name, bool negated) {
    NetEditResult result;
    if (!check_name(name, result)) {
        return result;
    }
    if (rung.timer || rung.counter) {
        return refused("a timer or counter rung has no coil");
    }
    rung.coils.push_back(Coil{std::move(name), negated});
    return {true, {}};
}

NetEditResult remove_coil(Rung& rung, std::size_t index) {
    if (index >= rung.coils.size()) {
        return refused("there is no coil at that index");
    }
    if (rung.coils.size() == 1 && !rung.timer && !rung.counter) {
        return refused("a rung needs a sink; remove the rung instead");
    }
    rung.coils.erase(rung.coils.begin() + static_cast<std::ptrdiff_t>(index));
    return {true, {}};
}

NetEditResult add_series(Rung& rung, const NetPath& path, std::string name, bool negated) {
    NetEditResult result;
    if (!check_name(name, result)) {
        return result;
    }
    if (!rung.condition) {
        if (!path.empty()) {
            return refused("path does not resolve");
        }
        rung.condition = make_contact(std::move(name), negated);
        return {true, {}};
    }

    Located at;
    if (!locate(rung.condition, path, at)) {
        return refused("path does not resolve");
    }
    if (at.node->kind == NetKind::Series) {
        at.node->operands.push_back(make_contact(std::move(name), negated));
        return {true, {}};
    }

    NetPtr wrapped =
        make_junction(NetKind::Series, std::move(slot(rung.condition, at)),
                      make_contact(std::move(name), negated));
    slot(rung.condition, at) = std::move(wrapped);
    return {true, {}};
}

NetEditResult add_parallel(Rung& rung, const NetPath& path, std::string name,
                           bool negated) {
    NetEditResult result;
    if (!check_name(name, result)) {
        return result;
    }
    if (!rung.condition) {
        return refused("a rail cannot gain a parallel branch");
    }

    Located at;
    if (!locate(rung.condition, path, at)) {
        return refused("path does not resolve");
    }
    if (at.node->kind == NetKind::Always) {
        return refused("a rail cannot gain a parallel branch");
    }
    if (at.node->kind == NetKind::Parallel) {
        at.node->operands.push_back(make_contact(std::move(name), negated));
        return {true, {}};
    }

    NetPtr wrapped =
        make_junction(NetKind::Parallel, std::move(slot(rung.condition, at)),
                      make_contact(std::move(name), negated));
    slot(rung.condition, at) = std::move(wrapped);
    return {true, {}};
}

NetEditResult toggle_negated(Rung& rung, const NetPath& path) {
    NetNode* node = find_node(rung, path);
    if (node == nullptr) {
        return refused("path does not resolve");
    }
    if (node->kind != NetKind::Contact) {
        return refused("only a contact can be negated");
    }
    node->negated = !node->negated;
    return {true, {}};
}

NetEditResult set_contact(Rung& rung, const NetPath& path, std::string name) {
    NetEditResult result;
    if (!check_name(name, result)) {
        return result;
    }
    NetNode* node = find_node(rung, path);
    if (node == nullptr) {
        return refused("path does not resolve");
    }
    if (node->kind != NetKind::Contact) {
        return refused("only a contact has a tag");
    }
    node->var = std::move(name);
    return {true, {}};
}

NetEditResult remove_node(Rung& rung, const NetPath& path) {
    if (path.empty()) {
        return refused("remove the rung instead of its condition");
    }

    Located at;
    if (!locate(rung.condition, path, at)) {
        return refused("path does not resolve");
    }

    NetPath parent_path(path.begin(), path.end() - 1);
    Located parent_at;
    if (!locate(rung.condition, parent_path, parent_at)) {
        return refused("path does not resolve");
    }

    at.parent->operands.erase(at.parent->operands.begin() +
                              static_cast<std::ptrdiff_t>(at.index));
    if (at.parent->operands.size() == 1) {
        NetPtr survivor = std::move(at.parent->operands.front());
        slot(rung.condition, parent_at) = std::move(survivor);
    }
    return {true, {}};
}

NetEditResult set_rail(Rung& rung, bool value) {
    auto node = std::make_unique<NetNode>();
    node->kind = value ? NetKind::Always : NetKind::Parallel;
    rung.condition = std::move(node);
    return {true, {}};
}

}  // namespace sc::plc
