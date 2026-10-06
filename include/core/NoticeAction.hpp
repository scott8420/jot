#pragma once
#include "core/Nodes.hpp"
#include "core/Notify.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/NoticeAction (s041) -- the buttons on a notification, as a VERB and a
// TARGET. Pure: no GTK, no Gio, no bus.
//
// ── why general ────────────────────────────────────────────────────────────
// The due notification is the first thing to carry buttons and it will not be
// the last: the packet's nudges (J2 -- "the receipt for the accountant is still
// missing") need the same three answers a deadline does: do it, not now, ask me
// again in a few days. So a button is not "the Mark done button"; it is a verb
// applied to a key, sent through ONE application action (`app.notice`), and the
// Shell applies it. A new kind of nudge adds a key shape and a button list, not
// a new action, a new D-Bus path or a new cold-start story.
//
// ── why the target is the KEY, not the node id ─────────────────────────────
// A notification is a promise made at one moment and answered at another. GNOME
// keeps it in the tray for hours; in between, the todo may have been ticked in
// jot, rescheduled, or -- the dangerous one -- a REPEATING todo may have rolled
// on to next week. "Mark done" from Monday's notice must not tick next
// Monday's occurrence. The announce key is `<id>@<due>`, so it names the
// occurrence, and check_notice_target() compares it with what the model says
// NOW. A stale key is refused, not guessed at. (ARC s011: "a write path from
// the tray into a model that may have moved since it was sent" -- this is the
// answer to that sentence.)
//
// ── snooze is about the NOTICE, done is about the TODO ─────────────────────
// Snooze and Remind do not touch the todo: no defer date written, nothing in
// jot.json, nothing a repeat could carry into the next occurrence. They take
// the key back out of the announced set and park it in a ledger until a time;
// due_announcements skips a parked key until then, and the sixty-second tick
// says it again when the time comes. The todo stays on Today the whole while,
// because the user said "not now" to an interruption, not to the work.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

enum class NoticeVerb {
    Done,     // tick the todo the key names -- if it is still that occurrence
    Snooze,   // say it again in `amount` MINUTES
    Remind,   // say it again in `amount` CALENDAR DAYS, same wall-clock time
    Got,      // s052: a packet nudge's "I've got it" -- tick item line `amount`
};

struct NoticeAct {
    NoticeVerb  verb   = NoticeVerb::Done;
    int         amount = 0;     // minutes (Snooze), days (Remind), unused (Done)
    std::string key;            // the announce key: "<id>@<due>"

    bool operator==(const NoticeAct&) const = default;
};

// The action's string parameter. "done:0:<key>", "snooze:60:<key>",
// "remind:1:<key>". Text rather than a tuple variant on purpose: it is what the
// log prints, what a test types, and what survives the daemon untouched.
std::string               encode_notice_act(const NoticeAct& a);
std::optional<NoticeAct>  decode_notice_act(std::string_view s);

// The id half of an announce key (everything before the LAST '@').
NodeId key_node(std::string_view key);

// One button on a notification, already worded.
struct NoticeButton {
    std::string label;
    NoticeAct   act;
};

// The buttons a due todo's notification carries: Mark done, In 1 hour,
// Tomorrow. GNOME draws at most three; this list is the whole of the policy and
// the Shell copies it onto the wire without deciding anything.
std::vector<NoticeButton> todo_notice_buttons(const Announcement& a);

// ── is the key still the thing it was when the notice went out? ───────────
enum class TargetState {
    Current,       // same todo, same due, not done -- act on it
    Gone,          // no such node (deleted, or another jots folder is open)
    NotTodo,       // it is a note now
    AlreadyDone,   // ticked since -- nothing to do, and say so
    Moved,         // its due changed (rescheduled, or a repeat rolled on)
};
TargetState check_notice_target(const NodeSource& src, std::string_view key);

// The words for a refusal, for the status line. Empty for Current.
std::string target_state_words(TargetState s);

// ── the snooze ledger ──────────────────────────────────────────────────────
// A key parked until an instant. Lives in Prefs beside `announced` (it is the
// same kind of thing -- what the notice side has said and agreed), survives a
// restart, and is pruned against the same `live` set so it cannot grow.
// (`Snoozed` itself is declared in core/Notify.hpp, because the announcer
// reads it.)

// When a Snooze / Remind act wants the key said again. Remind steps CALENDAR
// days (mktime with tm_isdst = -1), so "tomorrow" across a DST night is still
// the same time on the clock, not 23 or 25 hours on (s039's lesson).
std::int64_t notice_until(const NoticeAct& a, std::int64_t now);

// Park `key` until `until` (replacing any earlier park of the same key), and
// take it out of `announced` so it CAN be said again when the time comes.
void park(std::vector<Snoozed>& ledger, std::vector<std::string>& announced,
          const std::string& key, std::int64_t until);

// True if `key` is parked past `now`.
bool is_parked(const std::vector<Snoozed>& ledger, const std::string& key, std::int64_t now);

// Drop what has woken (until <= now) and what is no longer a deadline at all.
// Returns true if anything went, so the caller knows to save.
bool prune_parked(std::vector<Snoozed>& ledger, const std::vector<std::string>& live,
                  std::int64_t now);

}  // namespace jot::core
