#pragma once
#include "core/Nodes.hpp"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Routine (s057, J3's fourth strategy) -- WORK THAT COMES BACK.
//
// A repeating todo (s033) is a routine. s033 made it roll on and kept each
// occurrence in the history; this reads that history as the point: when it
// was last done, how regular it has been, and what has slipped.
//
//   on time   ticked by the end of its due DAY (a 09:00 due ticked at 18:00
//             the same day is on time -- the day is the promise, not the hour)
//   late      ticked on a later day; how many days late is kept
//   unknown   undated, or recorded before s057 kept the due
//   missed    the CURRENT occurrence's due has passed by one whole interval or
//             more: each interval gone by is an occurrence that did not
//             happen. "Slipped" is having any.
//
// GTK-free; the selftest owns every verdict.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

enum class Punct { OnTime, Late, Unknown };
const char* punct_word(Punct p);   // "ontime" "late" "unknown" -- CSS suffix and log word

struct Occurrence {
    std::int64_t when = 0;
    std::int64_t due  = 0;
    Punct        punct = Punct::Unknown;
    int          late_days = 0;
};

struct RoutineState {
    bool                    on = false;     // the todo repeats
    std::vector<Occurrence> recent;         // the last kWindow, oldest first
    int          total     = 0;             // every occurrence recorded
    int          on_time   = 0;             // of `recent`
    int          late      = 0;
    int          known     = 0;             // of `recent`, with a due to judge by
    std::int64_t last_done = 0;
    std::int64_t due       = 0;             // the current occurrence's (effective)
    int          missed    = 0;             // whole intervals gone by since `due`
    bool slipped() const { return missed > 0; }
};
inline constexpr int kRoutineWindow = 10;

RoutineState routine_state(const NodeSource& src, const NodeId& id, std::int64_t now);

// "9 of the last 10 on time  ·  last done Tue 29 Sep"; "Done 4 times  ·  last
// done ..." when none could be judged; "Not done yet" with no history.
std::string routine_line(const RoutineState& s, std::int64_t now);
// "Slipped — 2 missed since Mon 28 Sep"; "" when it has not.
std::string slipped_line(const RoutineState& s);
// The card's word: "slipped" / "9 of 10" / "" (no judged history yet).
std::string routine_short(const RoutineState& s);

// Every repeating todo that is not dropped, for the Projects tab's Routines
// list: slipped first (most missed first), then by the next due, undated last.
std::vector<NodeId> routine_list(const NodeSource& src, std::int64_t now);

}  // namespace jot::core
