#pragma once
#include "core/Nodes.hpp"
#include "core/Tasks.hpp"

#include <cstdint>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Forecast -- road item 10 (s039): what is coming, day by day.
//
// OmniFocus's Forecast, the jot way: a strip of days, and under each the todos
// DUE that day and the ones that START that day (their defer runs out, so they
// become something you could do). Overdue is not here -- the Today pane pins
// it above every view already, and a late thing is not "coming".
//
// THE RULES, each written down because each could have gone the other way:
//
//   due      the EFFECTIVE due (s007: the earliest in the chain), so a step in
//            a project due Friday shows on Friday -- the date it really has.
//            Due later TODAY counts for today; due before now is overdue and
//            pinned elsewhere.
//   starts   the effective defer, and only while it is still in the future: a
//            defer that has passed is just "available", nothing to forecast.
//   left     only what is left to do -- not done, not dropped. A held or
//            blocked todo still shows: its date is real, and the Today pane's
//            Overdue made the same call (a late thing you cannot start is the
//            one you most need to see).
//   days     calendar days, stepped by day_end + 1 second, not by 86400 -- a
//            DST night is 23 or 25 hours, and a fixed step drifts a bucket.
//
// GTK-free. `now` is passed in, like everywhere in core.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

struct ForecastDay {
    std::int64_t        day = 0;   // 00:00 local, the day's start
    std::vector<NodeId> due;       // document order
    std::vector<NodeId> starts;    // document order
    std::size_t count() const;     // distinct todos on the day
};

struct Forecast {
    std::vector<ForecastDay> days;    // today first, `ndays` of them, empty ones included
    std::vector<ForecastDay> later;   // after those, only days with something on them, in order
};

Forecast forecast(const NodeSource& src, const TaskIndex& tasks, std::int64_t now, int ndays = 7);

// The next day's 00:00 from any instant in a day -- DST-safe.
std::int64_t next_day(std::int64_t day);

}  // namespace jot::core
