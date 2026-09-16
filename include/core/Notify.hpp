#pragma once
#include "core/Nodes.hpp"
#include "core/Tasks.hpp"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Notify -- WHICH todos deserve an interruption, and which have already
// had one. Pure: no GTK, no Gio, no clock. The same split as core/Projection
// and for the same reason -- the decision is the part worth testing, and it is
// the part that must not be rewritten when the delivery mechanism turns out to
// be wrong. (s009 rewrote a delivery mechanism and `core::Projection` did not
// change a line. That is the whole argument for this file existing.)
//
// ── a projection is state; a notification is an EVENT ──────────────────────
// That difference drives everything below. A calendar row can be recomputed and
// rewritten forever because the desktop only ever shows the latest answer. A
// notification HAPPENS, once, at a moment, and there is no unringing it -- so
// this half has to remember what it already said, and that memory has to
// survive a restart or every launch re-announces the same overdue task.
//
// ── and it filters by availability, where the projection does NOT ──────────
// The deliberate asymmetry with core::Projection, which lets blocked and
// deferred todos through on purpose. A calendar card is a DAY GRID: dropping a
// row would make it lie about the day. A notification is an INTERRUPTION, and
// an interruption about something you cannot act on is precisely what teaches a
// person to turn notifications off -- at which point the ones that mattered are
// gone too.
//
// So: a grid may not lie by omission; a nudge may be selective. Same model, two
// honest rules, and they disagree because the surfaces answer different
// questions.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// One interruption, already decided and already worded.
struct Announcement {
    NodeId       id;                 // what the click goes to
    std::string  key;                // "<id>@<due>" -- identity of THIS due date
    std::string  summary;            // the title, or "Untitled"
    std::string  detail;             // the parent's title and the first line of its note
    std::int64_t due     = 0;
    bool         overdue = false;    // due before today, not merely before now
    bool         flagged = false;

    bool operator==(const Announcement&) const = default;
};

// ── why the key carries the due date ───────────────────────────────────────
// Keying by id alone would mean that moving a deadline -- the single most
// common edit a todo ever gets -- announced nothing, because the id had been
// seen before. Keying by id AND due instant means a rescheduled task is a new
// thing to be told about, which is what a person moving a date expects, and
// leaving it where it is stays quiet.
std::string announce_key(const NodeId& id, std::int64_t due);

// What to show now, and what the "already said" set becomes.
struct AnnounceResult {
    std::vector<Announcement> to_show;   // not in `announced`; show these
    std::vector<std::string>  keep;      // the new announced set, PRUNED
};

// ── the rule ───────────────────────────────────────────────────────────────
// A todo, not done, AVAILABLE, with an effective due date that has ARRIVED
// (due <= now), whose key is not already in `announced`.
//
// `keep` is every key that is currently due-and-available, announced or not --
// so the set cannot grow without bound, and a todo that is ticked off, deleted
// or rescheduled drops out of it. A todo that is un-ticked therefore announces
// again, which is correct: it became a thing to do again.
AnnounceResult due_announcements(const NodeSource& src, const TaskIndex& tasks,
                                 std::int64_t now,
                                 const std::vector<std::string>& announced);

}  // namespace jot::core
