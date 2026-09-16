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
//
// ── s015: `keep` no longer swallows what was merely SENT ───────────────────
// It used to be every key that was currently due -- which meant a key entered
// the announced set at the moment jot decided to speak, not at the moment
// anyone was told. A send that the daemon dropped was therefore recorded as
// said, once, forever. That is the whole bug this milestone is about, and the
// fix is a line of set algebra: `keep` is the pruned set of keys that were
// ALREADY delivered, and a key shown now joins it only when a receipt comes
// back (see `Outbox` below).
//
// `live` is what `keep` used to be -- every key that is currently a deadline.
// It is not the announced set; it is what the announced set and the delivery
// ledger are both pruned AGAINST, so neither can grow for the life of the
// folder.
struct AnnounceResult {
    std::vector<Announcement> to_show;   // not yet delivered; send these
    std::vector<std::string>  keep;      // delivered AND still live -- the new announced set
    std::vector<std::string>  live;      // every key currently due and available
};

// ── the rule ───────────────────────────────────────────────────────────────
// A todo, not done, AVAILABLE, with an effective due date that has ARRIVED
// (due <= now), whose key is not already in `announced`.
//
// `live` is every key that is currently due-and-available, announced or not --
// so nothing can grow without bound, and a todo that is ticked off, deleted or
// rescheduled drops out of it. A todo that is un-ticked therefore announces
// again, which is correct: it became a thing to do again.
AnnounceResult due_announcements(const NodeSource& src, const TaskIndex& tasks,
                                 std::int64_t now,
                                 const std::vector<std::string>& announced);

// ─────────────────────────────────────────────────────────────────────────────
// Outbox -- a send is not a delivery, and until s015 jot could not tell.
//
// `g_application_send_notification()` returns void. There is no failure it can
// report, so every send looked identical: the one that reached the daemon, the
// one the daemon dropped because it could not look the application id up, and
// the one that was never made at all because a hidden window had no application
// (s012). An evening went into that last one, and the trace channel said
// "notified:" through the whole of it.
//
// `org.gtk.Notifications.AddNotification` is the same request with a REPLY. So
// the rule becomes: a deadline is not announced until the daemon says it has
// it. That single change turns the existing sixty-second tick into a retry
// loop with no retry machinery anywhere -- an undelivered key is simply still
// undelivered next minute, and `due_announcements` offers it again.
//
// This is the ledger for the gap between asking and being answered. Pure: keys
// and counts, no bus, no clock.
// ─────────────────────────────────────────────────────────────────────────────

// How many refusals one announcement gets before jot stops asking. A daemon
// that refuses five times in five minutes is not busy, it is saying no -- and
// an unbounded retry would ask about a deadline from March for as long as the
// folder is open.
inline constexpr int kDeliveryTries = 5;

class Outbox {
public:
    enum class After { Retry, GiveUp };

    // A send is about to be made. False means one is ALREADY IN FLIGHT for this
    // key: the tick fires every sixty seconds and a slow daemon must not be
    // asked twice for the same row.
    bool begin(const std::string& key);

    // The daemon has it. Leaves the ledger -- the caller commits the key to the
    // announced set, and that is now the thing that keeps it quiet.
    void succeed(const std::string& key);

    // It refused. GiveUp when the tries are spent, and then this key leaves the
    // ledger too: the caller marks it said anyway rather than asking forever.
    After fail(const std::string& key);

    // Drop everything that is no longer a deadline. Ticking a todo off mid-
    // flight must not leave a count behind that a rescheduled version inherits.
    void prune(const std::vector<std::string>& live);

    bool        in_flight(const std::string& key) const;
    int         tries(const std::string& key) const;
    std::size_t size() const;

private:
    struct Entry {
        std::string key;
        int         tries  = 0;
        bool        flight = false;
    };
    std::vector<Entry> m_entries;

    Entry*       find(const std::string& key);
    const Entry* find(const std::string& key) const;
};

}  // namespace jot::core
