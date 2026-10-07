#pragma once
#include "core/Nodes.hpp"

#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/DoneWhen (s054, J3) -- a piece of work says what FINISHED means, and
// jot can check it. ("Done is a fact, not a feeling" -- ARCHITECTURE, What jot
// is FOR.)
//
// The packet (s044) was the first case: its checkbox lines are required items,
// each in by a file on its line or a tick. Done-when makes that rule a setting
// ANY todo or project can carry (Task::done_when), plus one more rule -- every
// step under it done. A packet is Items whatever the setting says: the packet
// is the first STRATEGY, and a strategy brings its done-when with it.
//
// ── what jot does with it ──────────────────────────────────────────────────
// Ticking (or Completing) work whose done-when is not met ASKS first -- "2 of
// 3 in -- tick anyway?" -- and the cards show the count. It never ticks by
// itself and never refuses: the tick is still the user's word, the question
// is the one moment it is cheap to notice something is missing.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// "tick" / "items" / "steps" -- jot.json's word and the log word.
const char* done_when_word(DoneWhen w);

// What rules THIS node: a packet is Items; otherwise its own setting.
DoneWhen effective_done_when(const Node& n);

struct DoneState {
    DoneWhen                 rule = DoneWhen::Tick;
    int                      in    = 0;
    int                      total = 0;
    std::vector<std::string> missing;    // item labels / step titles, in order
    // Nothing to count (just the tick, or no items / steps yet) is MET: an
    // empty list cannot be unfinished, and asking about it would only nag.
    bool met() const { return rule == DoneWhen::Tick || in >= total; }
    bool counted() const { return rule != DoneWhen::Tick && total > 0; }
};

DoneState done_state(const NodeSource& src, const NodeId& id);

// The count for a card or a row: "2 of 3 in" (Items), "2 of 5 steps"
// (Steps); "" when nothing is counted.
std::string done_count(const DoneState& s);

// The words in Note details' dropdown, in DoneWhen order:
// "Just the tick", "Every item is in", "Every step is done".
const std::vector<std::string>& done_when_choices();
// The line under it: what the rule means here, and where it stands.
std::string done_when_hint(DoneWhen w, bool packet);

// Does `after` FINISH work that `before` had not finished? A todo ticked, or a
// project Completed. (Dropped is giving up, not finishing: no question.)
bool finishes(const Task& before, const Task& after);

// ── the question ───────────────────────────────────────────────────────────
// For the nodes being finished whose done-when is not met: the dialog's words.
//   one:   "“Taxes 2026” is 2 of 3 in"            / "Missing: 1099-INT (bank)."
//   many:  "3 aren't finished yet"                 / one line each
// The button says what it will do: "Tick Anyway" (or "Complete Anyway" when
// every one is a project being Completed).
struct DoneAsk {
    std::string message;
    std::string detail;
    std::string button;
};
DoneAsk done_ask(const NodeSource& src, const std::vector<NodeId>& ids, bool completing);

}  // namespace jot::core
