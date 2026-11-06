#pragma once
// Editing operations on the netlist. A graphical editor does not rewrite text;
// it adds, removes and rewires nodes, so the model needs a small mutation API
// that keeps the tree valid and reports why an edit was refused.
//
// A path is the route from a rung's condition to a node: empty is the condition
// itself, and each element selects an operand of a junction. A path is valid
// only until the next structural edit, which is exactly the lifetime a click
// handler needs. Every operation returns an error instead of asserting, because
// a browser can send any index it likes.
#include "plc/netlist.hpp"

#include <string>
#include <vector>

namespace sc::plc {

using NetPath = std::vector<std::size_t>;

struct NetEditResult {
    bool ok = false;
    std::string error;
};

const NetNode* find_node(const Rung& rung, const NetPath& path);
NetNode* find_node(Rung& rung, const NetPath& path);

// A coil is replaced in place; the tag is the only thing the drawing edits.
NetEditResult set_coil(Rung& rung, std::size_t index, std::string name, bool negated);
NetEditResult add_coil(Rung& rung, std::string name, bool negated = false);
NetEditResult remove_coil(Rung& rung, std::size_t index);

// Adds a contact in series at `path`. A series at that spot grows; any other
// node is wrapped, so the result always means "the old network AND the new
// contact". A null condition is an empty rung, so the contact becomes the whole
// condition.
NetEditResult add_series(Rung& rung, const NetPath& path, std::string name,
                         bool negated = false);
// Adds a contact in parallel at `path`: the result means "the old network OR
// the new contact". A rail cannot gain a parallel branch, because it is already
// always true.
NetEditResult add_parallel(Rung& rung, const NetPath& path, std::string name,
                           bool negated = false);

NetEditResult toggle_negated(Rung& rung, const NetPath& path);
// Swaps an AND junction for an OR one and back. The operands are untouched, so
// the drawing keeps its shape and only the meaning of the block changes.
NetEditResult toggle_junction(Rung& rung, const NetPath& path);
NetEditResult set_contact(Rung& rung, const NetPath& path, std::string name);
// Removes a node below the root. A junction left with one operand collapses
// into that operand. The root is refused: remove the rung instead.
NetEditResult remove_node(Rung& rung, const NetPath& path);

// Replaces the whole condition with a rail. This is the "always true" and
// "always false" button on a drawing, not an edit of one node.
NetEditResult set_rail(Rung& rung, bool value);

}  // namespace sc::plc
