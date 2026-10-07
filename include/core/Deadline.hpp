#pragma once
#include "core/Nodes.hpp"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Deadline (s055, J3's second strategy) -- WORK BACK FROM THE DATE.
//
// A todo or project marked a deadline (Task::deadline) is read against its due
// date: what is left (its steps, their estimates) against the days left. The
// packet asks "what is missing?"; the deadline asks "is there still time?".
//
// ── what is left ───────────────────────────────────────────────────────────
// The same set done-when counts (core/DoneWhen), so the two never disagree:
//   Every item is in  -> each missing item is a step (items carry no estimate)
//   otherwise         -> each todo DIRECTLY under it not yet dealt with
//                        (done, Completed, Dropped are dealt with)
//   no todos under it -> the work itself is the one step (its own estimate)
//
// ── the rule, short enough to say on screen ─────────────────────────────────
// jot gives each step a day -- or each hour of estimated work a day, whichever
// is more -- plus one day spare. That is NEED. When the days left are no more
// than NEED, the deadline is RUNNING SHORT, and its next step is pulled
// forward onto Today. Before that, Note details says when to start by (the
// due day minus NEED). Past the due it is LATE, and pulled forward too.
//
// It never moves a date and never writes anything: a pull is a reading of the
// model at an instant, like Today itself.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

enum class Pace {
    NoDate,     // marked, but no due date to work back from
    Finished,   // ticked / Completed / Dropped -- nothing to say
    Ready,      // nothing left but the tick itself
    OnTrack,    // more days than the work needs
    Short,      // the days left are what the work needs, or fewer
    Late        // the due has passed
};
const char* pace_word(Pace p);   // "nodate" "finished" "ready" "ontrack" "short" "late"

struct DeadlineState {
    Pace         pace      = Pace::NoDate;
    std::int64_t due       = 0;   // effective (a parent's counts)
    int          days_left = 0;   // calendar days, today -> the due day; <0 late
    int          steps     = 0;   // left
    int          minutes   = 0;   // estimated, over what is left
    int          unsized   = 0;   // steps with no estimate
    int          need      = 0;   // days the work wants, the spare day included
    std::int64_t start_by  = 0;   // 00:00 of the due day minus `need` days
    NodeId       next;            // the step to do now ("" = nothing can start);
                                  // the deadline itself when it is its own step
    std::string  next_title;
    bool pulls() const { return (pace == Pace::Short || pace == Pace::Late) && !next.empty(); }
};

// Only meaningful for a node with Task::deadline set; for any other node the
// answer is still computed (the drawer shows it greyed when off).
DeadlineState deadline_state(const NodeSource& src, const NodeId& id, std::int64_t now);

// "3 steps · ~4h · 9 days out", "1 step · due today", "2 steps · ~1h (1 not
// sized) · 3 days late". "" for NoDate / Finished.
std::string runway_text(const DeadlineState& s);

// The second line -- what it means:
//   NoDate   "Give it a due date and jot works back from it."
//   Ready    "Everything is done -- tick it."
//   OnTrack  "On track -- start by Thu 8 Oct  ·  about 30m a day"
//            ("a week" when more than two weeks out; rounded up to 5 minutes)
//   Short    "Running short -- “Book the van” is on Today"
//   Late     "Late -- “Book the van” is on Today"
// (Short / Late with nothing startable: "... -- nothing can start yet".)
std::string pace_text(const DeadlineState& s, std::int64_t now);

// The mark's words where no due chip can wear the hourglass (a note project):
// "⏳ 9d" on track, "⏳ short", "⏳ late", "⏳ ready".
std::string deadline_mark(const DeadlineState& s);

// Every deadline that is running short or late and has a step that can start,
// in document order: what Today pins under "Running short".
struct Pull {
    NodeId deadline;
    NodeId step;
};
std::vector<Pull> deadline_pulls(const NodeSource& src, std::int64_t now);

}  // namespace jot::core
