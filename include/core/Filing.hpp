#pragma once
#include "core/Nodes.hpp"

#include <cstddef>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Filing -- where a note can go, for "Move to..." (s029).
//
// s028 gave jot an Inbox and Clean Up, but the one verb that PROCESSES a
// capture -- put it where it belongs -- was a drag across the tree. With fifty
// notes that is fine; with five hundred, the target is three screens down a
// collapsed outline and the drag is a scroll-hunt. OmniFocus's answer is a
// Move picker you type into, and this is the half of it that decides things:
//
//   move_targets   every legal destination for `moving`, each with the path
//                  that tells two "Notes" apart, filtered by what was typed
//   remember_target  the few places you filed into last, shown first
//
// THE MODEL DECIDES LEGALITY, NOT THE PICKER. A target is offered only when
// core::can_move says yes -- so the picker cannot offer a cycle (a note into
// its own child) or move a protected note, for the same reason the drag
// cannot. The note's CURRENT parent is left out: "move it where it already
// is" is a no-op dressed as a choice.
//
// TOP LEVEL is a target (empty id) whenever the note is not already there.
//
// GTK-free; the selftest owns every verdict here.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

struct MoveTarget {
    NodeId      id;      // empty == the top level
    std::string title;   // "Untitled" for an untitled note; "Top level" for the root
    std::string path;    // the ancestors, outermost first: "Work › Taxes"; "" at depth 0
};

// The label the root target carries, so the picker and the selftest agree.
inline constexpr const char* kTopLevel = "Top level";

// Every place `moving` may go, ranked for `query`:
//   empty query    Top level first (when legal), then tree order
//   a query        case-insensitive (ASCII); titles that START with it, then
//                  titles that CONTAIN it, then notes whose PATH contains it
//                  (typing "taxes" finds the notes inside Taxes too). Tree
//                  order within each group. Top level matches "top".
// `moving` unknown or protected -> empty: there is nowhere it can go.
std::vector<MoveTarget> move_targets(const NodeSource& src, const NodeId& moving,
                                     const std::string& query);

// The path as move_targets writes it, for one node: its ancestors' titles,
// outermost first, joined by " › ". Exposed so the Inbox row and the picker
// name a place the same way.
std::string node_path(const NodeSource& src, const NodeId& id);

// Most-recent-first list of the places filed into, capped at `cap`. Moving
// `target` to the front if it is already there. The top level (empty id) is
// not remembered -- it is always one keystroke away at the head of the list.
void remember_target(std::vector<NodeId>& recent, const NodeId& target, std::size_t cap = 5);

// The remembered places that are still legal targets for `moving`, in recent
// order -- a remembered note may have been deleted, or be the very note being
// moved, or be its current parent.
std::vector<MoveTarget> recent_targets(const NodeSource& src, const NodeId& moving,
                                       const std::vector<NodeId>& recent);

}  // namespace jot::core
