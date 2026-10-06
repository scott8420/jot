#pragma once
#include "core/Format.hpp"
#include "core/Nodes.hpp"
#include "core/Repeat.hpp"

#include <cstdint>
#include <string>
#include <utility>

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

// ── what several notes SHARE (s049) ────────────────────────────────────────
// Scott's ask, second half: with several selected, Note details shows what
// they have in common and a set reaches all of them. A field every one agrees
// on shows as itself; one they disagree on is MIXED (the field blank, saying
// "mixed"). The todo fields are read over the TODOS among them only -- a plain
// note has no due date to disagree with -- and a write goes to the todos only.
template <class T>
struct Shared {
    bool mixed = false;
    T    value{};
};

struct Common {
    std::size_t count  = 0;   // known notes among the ids
    std::size_t todos  = 0;   // of them, todos
    std::size_t locked = 0;   // of them, protected (a write leaves these alone)
    Shared<bool>         is_task;
    // Over the todos only; meaningless when todos == 0.
    Shared<bool>         done, flagged;
    Shared<std::int64_t> due, defer;
    Shared<int>          estimate;
    Shared<Repeat>       repeat;   // the RULE (every + unit); from_done compared too
    // Tags by key, as first written. `tags_all`: every note carries it.
    // `tags_some`: some do -- with how many.
    std::vector<std::string>                         tags_all;
    std::vector<std::pair<std::string, std::size_t>> tags_some;
};
Common common(const NodeSource& src, const std::vector<NodeId>& ids);

// The notes a set over the selection writes to, in the order given: known,
// not protected, and todos only when `todos_only`.
std::vector<NodeId> writable(const NodeSource& src, const std::vector<NodeId>& ids, bool todos_only);

// The tag-line edits that put #name on (add) or take it off (remove) every
// writable note in `ids` -- only the ones it changes. Each is the edit for
// that note's body as stored (core::apply turns it into the new body).
std::vector<std::pair<NodeId, FmtEdit>> tag_edits(const NodeSource& src, const std::vector<NodeId>& ids,
                                                  const std::string& name, bool add);

// "Due date" on one note, "Due date 3 notes" on several: the step's name.
std::string many_label(const std::string& what, std::size_t n);

}  // namespace jot::core
