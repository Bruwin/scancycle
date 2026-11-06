#include "harness.hpp"

#include "plc/compiler.hpp"
#include "plc/netlist_edit.hpp"
#include "plc/vm.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sc::plc;

NetPtr contact_node(std::string name, bool negated = false) {
    auto node = std::make_unique<NetNode>();
    node->kind = NetKind::Contact;
    node->var = std::move(name);
    node->negated = negated;
    return node;
}

Rung rung_with(std::string tag) {
    Rung rung;
    rung.condition = contact_node(std::move(tag));
    rung.coils.push_back(Coil{"d", false});
    return rung;
}

// Compiles one rung with a, b and c declared and returns d's truth-table column.
std::string column(Rung rung, const std::vector<std::string>& inputs) {
    std::vector<TagDecl> vars;
    for (const char* name : {"a", "b", "c", "d"}) {
        vars.push_back(TagDecl{name, ValueType::Bool});
    }
    std::vector<Rung> rungs;
    rungs.push_back(std::move(rung));

    const NetlistResult built = compile_netlist("Edit", rungs, vars);
    CHECK(built.ok);
    if (!built.ok) {
        return {};
    }

    Vm vm(built.program);
    std::string out;
    const std::size_t rows = std::size_t{1} << inputs.size();
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t bit = 0; bit < inputs.size(); ++bit) {
            vm.set(inputs[bit], Value::boolean(((row >> bit) & 1u) != 0));
        }
        vm.scan(static_cast<sc::i64>(row) * 10);
        out += vm.get("d").as_bool() ? '1' : '0';
    }
    return out;
}

const NetNode* root(const Rung& rung) { return find_node(rung, {}); }

}  // namespace

TEST(add_series_wraps_a_contact_and_ands_it) {
    Rung rung = rung_with("a");
    const NetEditResult edit = add_series(rung, {}, "b");
    CHECK(edit.ok);
    CHECK(root(rung) != nullptr);
    CHECK(root(rung)->kind == NetKind::Series);
    CHECK(column(std::move(rung), {"a", "b"}) == "0001");
}

TEST(add_parallel_wraps_a_contact_and_ors_it) {
    Rung rung = rung_with("a");
    const NetEditResult edit = add_parallel(rung, {}, "b");
    CHECK(edit.ok);
    CHECK(root(rung)->kind == NetKind::Parallel);
    CHECK(column(std::move(rung), {"a", "b"}) == "0111");
}

TEST(add_series_after_a_parallel_wraps_the_whole_branch) {
    Rung rung = rung_with("a");
    CHECK(add_parallel(rung, {}, "b").ok);
    CHECK(add_series(rung, {}, "c").ok);

    CHECK(root(rung)->kind == NetKind::Series);
    const NetNode* branch = find_node(rung, {0});
    CHECK(branch != nullptr);
    if (branch != nullptr) {
        CHECK(branch->kind == NetKind::Parallel);
    }
    CHECK(column(std::move(rung), {"a", "b", "c"}) == "00000111");
}

TEST(toggle_negated_flips_only_a_contact) {
    Rung rung = rung_with("a");
    CHECK(toggle_negated(rung, {}).ok);
    CHECK(find_node(rung, {})->negated);
    CHECK(column(std::move(rung), {"a"}) == "10");
}

TEST(remove_node_collapses_a_series_to_its_survivor) {
    Rung rung = rung_with("a");
    CHECK(add_series(rung, {}, "b").ok);
    CHECK(remove_node(rung, {0}).ok);

    const NetNode* node = root(rung);
    CHECK(node != nullptr);
    if (node != nullptr) {
        CHECK(node->kind == NetKind::Contact);
        CHECK(node->var == "b");
    }
}

TEST(removing_the_root_condition_is_refused) {
    Rung rung = rung_with("a");
    const NetEditResult edit = remove_node(rung, {});
    CHECK(!edit.ok);
    CHECK(edit.error.find("remove the rung") != std::string::npos);
}

TEST(set_rail_replaces_the_condition_with_a_constant) {
    Rung always = rung_with("a");
    CHECK(set_rail(always, true).ok);
    CHECK(root(always) != nullptr);
    CHECK(root(always)->kind == NetKind::Always);
    CHECK(column(std::move(always), {}) == "1");

    Rung never = rung_with("a");
    CHECK(set_rail(never, false).ok);
    CHECK(root(never)->kind == NetKind::Parallel);
    CHECK(root(never)->operands.empty());
    CHECK(column(std::move(never), {}) == "0");
}

TEST(a_rail_cannot_gain_a_parallel_branch) {
    Rung rung = rung_with("a");
    CHECK(set_rail(rung, true).ok);
    const NetEditResult edit = add_parallel(rung, {}, "b");
    CHECK(!edit.ok);
    CHECK(edit.error.find("rail") != std::string::npos);
}

TEST(coils_can_be_added_renamed_and_negated) {
    Rung rung = rung_with("a");
    CHECK(add_coil(rung, "e").ok);
    CHECK(rung.coils.size() == 2);

    CHECK(set_coil(rung, 1, "f", true).ok);
    CHECK(rung.coils[1].var == "f");
    CHECK(rung.coils[1].negated);

    CHECK(remove_coil(rung, 1).ok);
    CHECK(rung.coils.size() == 1);
    CHECK(!remove_coil(rung, 0).ok);
}

TEST(a_path_that_does_not_resolve_is_refused) {
    Rung rung = rung_with("a");
    CHECK(!add_series(rung, {3}, "b").ok);
    CHECK(!toggle_negated(rung, {0}).ok);
    CHECK(!set_contact(rung, {0}, "b").ok);
    CHECK(!remove_node(rung, {0, 1}).ok);
}

TEST(swapping_a_junction_flips_its_meaning) {
    Rung rung = rung_with("a");
    CHECK(add_parallel(rung, {}, "b", false).ok);
    CHECK(root(rung)->kind == NetKind::Parallel);

    CHECK(toggle_junction(rung, {}).ok);
    CHECK(root(rung)->kind == NetKind::Series);
    CHECK(column(std::move(rung), {"a", "b"}) == "0001");
}

TEST(a_contact_has_no_junction_to_swap) {
    Rung rung = rung_with("a");
    CHECK(!toggle_junction(rung, {}).ok);
    CHECK(root(rung)->kind == NetKind::Contact);
}
