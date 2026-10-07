#pragma once
#include "core/Nodes.hpp"
#include "core/Routine.hpp"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Feeders (s058, J4) -- THE SMALL REPEATING STEPS THAT FEED A GOAL.
//
// Scott (Oct 2026): "prep for taxes might be less painful if you look out at
// future work and plan ahead. Scan receipts leads to organize for the
// accountant." A feeder is a todo -- usually a routine -- whose Task::feeds
// names the goal it serves. The goal is usually a deadline (s055); it need
// not be.
//
// What the goal can then say:
//   who feeds it      its feeders, in tree order (dropped ones left out)
//   how they keep up  each feeder's routine record (s057) -- slipped or not
//   what is to come   the occurrences each feeder still has before the
//                     goal's due, times its estimate: "~6h before the due",
//                     and spread over the weeks left, "~15m a week"
//
// Reading only. The link lives on the feeder, so a goal deleted or renamed
// costs nothing: a feeder whose goal is gone feeds nothing.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

struct FeederState {
    NodeId       id;
    std::string  title;
    RoutineState routine;     // .on false: a one-off feeder (no repeat)
    bool         done = false;   // a one-off that is finished
    int          to_come = 0;    // occurrences still due on or before the goal's due
    int          minutes = 0;    // to_come x its estimate
};

struct FeedState {
    std::vector<FeederState> feeders;
    int          slipped = 0;
    int          minutes = 0;    // over every feeder, before the goal's due
    int          unsized = 0;    // feeders with occurrences to come and no estimate
    int          weeks   = 0;    // whole weeks to the goal's due (0 = none, or under a week)
    std::int64_t due     = 0;    // the goal's effective due
};

// The feeders of `goal`, in tree order, not dropped.
std::vector<NodeId> feeders_of(const NodeSource& src, const NodeId& goal);

FeedState feed_state(const NodeSource& src, const NodeId& goal, std::int64_t now);

// "2 feeders  ·  ~6h before the due  ·  ~15m a week  ·  1 slipped"
// "" when there are none.
std::string feed_line(const FeedState& f);

// One feeder's record for the goal's list (the caller puts the rule before
// it: "every week  ·  " + this):
//   "slipped, 7 missed" / "last done Mon 14 Sep" / "not done yet"
//   "once  ·  done" / "once  ·  to do"      (not a routine: the whole line)
std::string feeder_line(const FeederState& s);

// Every node that could be fed, for the picker: all but `feeder` itself,
// deadlines first (then tree order), filtered by `query` the way Move to...
// filters. Each with its path.
struct FeedTarget {
    NodeId      id;
    std::string title;
    std::string path;
    bool        deadline = false;
};
std::vector<FeedTarget> feed_targets(const NodeSource& src, const NodeId& feeder,
                                     const std::string& query);

}  // namespace jot::core
