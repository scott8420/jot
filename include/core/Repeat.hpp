#pragma once
#include <cstdint>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// core/Repeat -- a todo that comes back (s033, road item 6).
//
// The rule lives on the todo (Task::repeat). Ticking it does NOT finish the
// note: MemoryNodes::set_task logs this occurrence to the history (so the
// Logbook shows it) and rolls the SAME note forward -- dates moved, tick
// cleared, a project's steps unticked. Same id, same body, same place in the
// tree, same links pointing at it. A repeating todo that left a ticked copy
// behind every time would fill the tree with the past.
//
// Two ways to count, as OmniFocus has:
//   fixed      (default) the next due is the old due plus the interval --
//              rent is due on the 1st whether you paid on the 1st or the 5th.
//              Missed occurrences are skipped: the next one is the first
//              AFTER now, because a todo that comes back already late
//              three times over is noise, not a reminder.
//   from done  the next due is the interval after the day it was DONE, at
//              the old due's time of day -- water the plants a week after you
//              last watered them.
// The defer date moves by the same amount, so the gap between "show it" and
// "due" is kept.
//
// GTK-free. Pure: `now` is passed in.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

enum class RepeatUnit { Day, Week, Month, Year };

struct Repeat {
    int        every     = 0;               // 0 == does not repeat
    RepeatUnit unit      = RepeatUnit::Day;
    bool       from_done = false;

    bool on() const { return every > 0; }
    bool operator==(const Repeat&) const = default;
};

// "every 2 weeks", "every day", "" for none. What the drawer shows and what
// jot.json stores; repeat_parse reads it back.
std::string repeat_text(const Repeat& r);

// Accepted (any case, extra spaces fine): "" / "none" / "never" -> off;
// "daily" "weekly" "fortnightly" "monthly" "yearly" "annually";
// "[every] [N|other] day(s)/week(s)/month(s)/year(s)". `from_done` is not in
// the text -- it is its own switch -- and is left as `out` had it.
// false (and `out` untouched) when it does not parse.
bool repeat_parse(const std::string& text, Repeat& out);

// `when` moved forward by k intervals, in LOCAL calendar terms: a day is a
// calendar day (DST-safe), a month keeps the day of the month and clamps to
// the month's end (Jan 31 -> Feb 28), a year clamps Feb 29.
std::int64_t repeat_add(std::int64_t when, const Repeat& r, int k = 1);

// The next occurrence's dates. Either date may be 0; with both 0 there is
// nothing to move and both stay 0 (the todo just comes back unticked).
void repeat_next(const Repeat& r, std::int64_t now, std::int64_t& due, std::int64_t& defer);

}  // namespace jot::core
