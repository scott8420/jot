#pragma once
#include "core/Nodes.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Errands (s056, J3's third strategy) -- THE ERRAND RUN.
//
// Scott (Oct 2026): "batching tasks by the location where they must be done --
// looking at what else is nearby to get future tasks done in one go." Work
// bunched by WHERE, so one trip clears what can be cleared there.
//
// ── a place is a tag ───────────────────────────────────────────────────────
// Under `#at/`: `#at/town`, `#at/hardware-store`. Tags stay text in the note
// (s035's rule), so a place needs nothing new in the model, travels with the
// note, and Tags › #at already lists every place. Nesting rolls up:
// `#at/town/bank` is an errand IN TOWN, its sub-place ("bank") said on its
// card -- one trip into town covers the bank. A todo with two places is an
// errand in both (either trip can do it).
//
// ── what a run adds up to ───────────────────────────────────────────────────
// The errands you can do now (available: not done, deferred, blocked or on
// hold), with their estimates summed and the unsized counted -- s043's rule,
// never a guess -- and how many are late or due this week (within 6 days),
// which is what tells you which trip to make first. The ones not yet doable
// are counted ("2 later"), not listed.
//
// GTK-free; the selftest owns every verdict.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// "at/town" -> true; "at" alone, "attic", "home/at/x" -> false.
bool is_place_key(std::string_view key);

struct Errand {
    NodeId      id;
    std::string sub;   // "bank" for #at/town/bank in Town's run; "" for #at/town
    std::string also;  // the other places it is an errand in: "Hardware store"
};

struct ErrandRun {
    std::string         key;     // "at/town"
    std::string         name;    // "Town" -- as first written, capitalised
    std::vector<Errand> errands; // doable now: soonest due first, then tree order
    int          later    = 0;   // open but not doable yet (deferred, blocked, on hold)
    int          minutes  = 0;   // estimates summed, over `errands`
    int          unsized  = 0;
    int          late     = 0;   // past due
    int          week     = 0;   // due within six days (today included), not late
    std::int64_t first_due = 0;  // the soonest effective due among `errands`; 0 = none
};

// Every place with an open errand. Order: the run with the soonest due first
// (a late one before all), then the undated by name, then places with nothing
// doable yet.
// s060: the places a node is tagged with, as run keys ("at/town" for both
// #at/town and #at/town/bank), each once, in the order written; and a run
// key's name as the Errands view says it ("Town", "Hardware store").
std::vector<std::string> node_places(const Node& n);
std::string              place_title(std::string_view key);

std::vector<ErrandRun> errand_runs(const NodeSource& src, std::int64_t now);

// "4 errands  ·  ~1h 20m (1 not sized)  ·  1 late  ·  2 due this week"
// "Nothing to do there yet  ·  2 later"
std::string run_summary(const ErrandRun& r);

// The view's line under its title: "3 places  ·  9 errands  ·  ~2h 10m".
// Errands are counted ONCE however many runs they are on (a todo with two
// places is one thing to do), and so is their time.
std::string runs_summary(const NodeSource& src, const std::vector<ErrandRun>& runs);

}  // namespace jot::core
