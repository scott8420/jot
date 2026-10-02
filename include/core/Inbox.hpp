#pragma once
#include "core/Nodes.hpp"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Inbox -- what is waiting to be processed, and Clean Up (s028).
//
// s007 said "there is no Inbox: unfiled is a state, not a place to empty".
// s028 overturns it, and the reason is the one OmniFocus is built on: a
// capture is a promise to come back, and a promise needs a list it is kept on.
// A top-level note is not that list -- plenty of notes belong at the top level
// for good, and nothing told them apart from a thought dumped there at 2 am.
//
// THE INBOX IS A MARK, NOT A PLACE. `Node::inbox` is set by every capture road
// (core::capture, and capture_list / capture_append when they MAKE a note).
// The node sits in the tree wherever it is; the Inbox is a query over the mark.
//
// FILING DOES NOT REMOVE IT. A marked node that has been dealt with -- moved
// under a parent, or a todo ticked done -- shows as PROCESSED and stays on the
// list until Clean Up. That is OmniFocus's design and it is the right one: a
// row that vanished the moment you dragged it would leave you asking where it
// went, and the list you were working down would reflow under your hand.
//
// Clean Up clears the mark on processed nodes and touches nothing else. It
// moves nothing, deletes nothing, edits no body.
//
// GTK-free; the selftest owns every verdict here.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

enum class InboxState {
    Waiting,   // marked, still at the top level, not done: needs a decision
    Filed,     // marked, but now has a parent: processed, Clean Up takes it
    Done,      // marked, a todo ticked done: processed, Clean Up takes it
};

// The verdict for one node. Meaningful only for a marked node; an unmarked one
// answers by the same rules (the pane never asks).
InboxState inbox_state(const Node& n);
inline bool inbox_processed(const Node& n) { return inbox_state(n) != InboxState::Waiting; }

// Every marked node, in TREE ORDER (depth first, as the rows are drawn) --
// so the Inbox lists things in the order you would meet them in the tree,
// and a capture made later appears below one made earlier at the top level.
std::vector<NodeId> inbox_members(const NodeSource& src);

struct InboxCounts {
    std::size_t waiting = 0;   // what the tab's number says
    std::size_t ready   = 0;   // what Clean Up would take
};
InboxCounts inbox_counts(const NodeSource& src);

// Clear the mark on every processed node. Returns how many were cleared.
// Waiting nodes are left exactly as they were.
std::size_t clean_up(NodeSource& src);

// "just now", "4 min ago", "3 h ago", "1 day ago", "5 days ago", then the date.
// How long a capture has been waiting -- the one fact about it the Inbox can
// say that the tree cannot. `then` == 0 -> "".
std::string age_phrase(std::int64_t then, std::int64_t now);

}  // namespace jot::core
