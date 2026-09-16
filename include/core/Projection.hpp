#pragma once
#include "core/Nodes.hpp"
#include "core/Tasks.hpp"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Projection -- what the desktop is told, decided here and nowhere else.
//
// s009 puts jot's dated todos in the GNOME calendar dropdown by writing them
// into an Evolution Data Server task list. THE DECISION of what goes there, and
// what each line says, is a pure function of the model and an instant -- so it
// lives in core, under the selftest, GTK-free and libecal-free.
//
// What is left for the backend is only the writing: turn these records into
// VTODOs and put them in the list. A backend that DECIDED anything would be a
// second answer to a question core already answers, across a process boundary,
// where nothing can see it disagree. That is the shape of bug this milestone
// is explicitly built to avoid.
//
// ── ONE WAY, ALWAYS ────────────────────────────────────────────────────────
// jot owns the truth. The EDS list is a PROJECTION: wiped and rewritten from
// this vector, never read back. Ticking something off in the desktop pill does
// nothing to jot, deliberately. Two-way sync into another app's database, where
// the selftest cannot reach and a stale answer surfaces on the desktop rather
// than in a panel you can refresh, is not a class of bug this app should own.
// It also keeps the whole feature deletable: remove the list, nothing is lost.
//
// ── WHAT PROJECTS, AND WHY THAT RULE ───────────────────────────────────────
// A todo, not done, WITH AN EFFECTIVE DUE DATE. That is the whole rule.
//
//   a todo         -- notes have nothing to put on a day.
//   not done       -- the card answers "what is on", not "what was".
//   an effective   -- the date may be the node's own or inherited from an
//   due date          ancestor (core::effective_due). A task inside a project
//                     due Friday is due Friday, and the desktop should say so.
//
// NOT filtered by availability, and that is the interesting choice. Blocked and
// deferred todos still project. The calendar card is a DAY GRID: it answers
// "what falls on this day", and a due date is a fact about a day whatever jot
// thinks of the task's readiness. Availability is jot's question, answerable
// only next to the why -- which is in Today, where the parent's prose is. A
// projection that quietly dropped rows would be a desktop that lies about the
// day, and the lie would be invisible from inside jot.
//
// So availability is not a FILTER here; it is a WORD in the description, which
// is the honest place for it.
//
// Undated todos do not project at all -- flagged or not. They have no day, and
// inventing one (today, say) would put jot's opinion on the desktop's grid.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// One line as the desktop will see it. Everything the backend needs, already
// decided; the backend does no lookups of its own.
struct Projected {
    NodeId       id;                 // jot's node -- the origin, for the log
    std::string  uid;                // "jot-<id>": stable, and marks the row OURS
    std::string  summary;            // the title, or "Untitled"
    std::string  detail;             // availability + the why (parent's prose)
    std::int64_t start   = 0;        // what DTSTART becomes. See below.
    std::int64_t due     = 0;        // effective due, epoch seconds
    bool         all_day = false;    // the date was typed bare (no time)
    bool         overdue = false;    // due before `now`
    bool         flagged = false;

    bool operator==(const Projected&) const = default;
};

// ── why there are two times, not one ───────────────────────────────────────
// `due` is what the FORMAT means: a VTODO's DUE is its deadline, and any other
// reader of the list expects it there.
//
// `start` is what the DESKTOP READS. gnome-shell-calendar-server takes a
// component's start time from DTSTART and gives up (start time 0, filtered out
// of the day range) when there is none -- a VTODO carrying only DUE never
// reaches the card. So jot writes DTSTART as well, and this field is what goes
// in it:
//
//   all-day  -> the START of the due day. With a DATE value the server adds a
//               day for the end, so the row covers the day, which is what a
//               thing "due Tuesday" means.
//   timed    -> the due instant itself, so it sits at its time in the day.
//
// This asymmetry is the single non-obvious thing in the milestone and it is
// empirical -- read out of the calendar server's source, not deduced from the
// spec. (CANON: verify before assuming fit; ask the interface, not the spec.)

// The whole decision, in document order (the TaskIndex's order).
std::vector<Projected> project(const NodeSource& src, const TaskIndex& tasks,
                               std::int64_t now);

// "jot-<id>". The ONE place the prefix is spelled: the writer creates rows with
// it and the wipe deletes rows with it, and if those two ever disagreed jot
// would either orphan its own rows or delete another app's.
std::string projection_uid(const NodeId& id);
const char* projection_prefix();

// A cheap fingerprint of a whole projected set.
//
// Every model write refreshes the task index, and a sync is a D-Bus round trip
// into another process -- so "has anything the desktop can see actually
// changed?" has to be answerable without doing the write. Typing in a note's
// body changes the model constantly and changes this not at all.
//
// It is a fingerprint, not a cache of the answer: the projection is still
// recomputed from the model every time. The only thing skipped is the writing.
std::string projection_signature(const std::vector<Projected>& items);

}  // namespace jot::core
