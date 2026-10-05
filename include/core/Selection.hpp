#pragma once
#include "core/Nodes.hpp"

#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Selection (s047) -- the arithmetic of acting on SEVERAL notes at once.
// Scott: "multi select to delete and set common metadata". Pure: no GTK.
//
// ── roots, not rows ────────────────────────────────────────────────────────
// Select "Taxes" and "W-2" (which is under Taxes) and press Delete: W-2 goes
// with Taxes already, and asking the model to delete it a second time would be
// a refusal in the log for something the user did not do wrong. A move is the
// same: moving Taxes carries W-2, and moving W-2 as well would pull it out from
// under its parent. So every verb that takes a subtree acts on the ROOTS of the
// selection -- the selected notes with no selected ancestor.
//
// ── a toggle over many ─────────────────────────────────────────────────────
// Tick with three todos selected, one already done: the Files / mail rule --
// if ANY is off, all go on; only when every one is on do all go off. So one
// press never splits a selection into two states.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// `ids` (unknown ones dropped, duplicates once) in DOCUMENT order -- a
// pre-order walk of the tree, the order the rows are drawn.
std::vector<NodeId> in_document_order(const NodeSource& src, const std::vector<NodeId>& ids);

// The selected notes that have no selected ancestor, in document order.
std::vector<NodeId> selection_roots(const NodeSource& src, const std::vector<NodeId>& ids);

// Where the selection goes when all of `ids` are deleted (ask BEFORE): the
// first sibling after the last root that is not itself going, else the
// nearest one before the first root, else the first root's parent. Empty if
// nothing would be left to stand on.
NodeId neighbour_after_delete_all(const NodeSource& src, const std::vector<NodeId>& ids);

// The toggle rule above: true (turn on) unless every one already is.
template <class Pred>
bool turn_on(const NodeSource& src, const std::vector<NodeId>& ids, Pred is_on) {
    for (const auto& id : ids)
        if (const Node* n = src.find(id); n && !is_on(*n)) return true;
    return false;
}

}  // namespace jot::core
