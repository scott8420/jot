#pragma once
#include "core/Nodes.hpp"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/RowLook (s042, J1 "the look") -- WHAT a todo's card says, decided once.
// Pure: no GTK. The card widget (TaskCard) only maps this onto labels and CSS
// classes, so Today, Forecast and Tags cannot disagree about a row's colour or
// its words -- one builder, one decision.
//
// ── colour carries meaning, so the meaning is decided here ──────────────────
// Every card has ONE state, and the state picks the edge stripe and the tick's
// ring. The order below is the precedence: the first that is true wins.
//
//   Done      ticked                                   (green)
//   Overdue   its due has passed                       (red)
//   DueToday  due before the end of today              (orange)
//   OnHold    it, or its project, is on hold           (grey)
//   Waiting   blocked behind an earlier step           (grey)
//   Deferred  not startable yet                        (grey)
//   Flagged   flagged, itself or by its project        (amber)
//   Available do it whenever                           (blue)
//
// Overdue before OnHold on purpose: "late" is the news even when it is paused
// -- the same reason Today pins Overdue above every view.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

enum class RowState { Done, Overdue, DueToday, OnHold, Waiting, Deferred, Flagged, Available };

// "done", "overdue", "today", "hold", "waiting", "deferred", "flagged",
// "available" -- the CSS class suffix (`st-<word>`) and the log word.
const char* row_state_word(RowState s);

struct RowLook {
    RowState    state = RowState::Available;
    std::string due;            // the chip on the right: "Today 17:00", "Tue", "3d late"; "" if undated
    bool        due_inherited = false;    // the date is a parent's
    bool        flagged = false;          // itself or a parent
    std::string estimate;       // "45m", "" if none
    std::string repeat;         // "every week", "" if none
    std::string project;        // the parent's title, "" at top level
};

RowLook row_look(const NodeSource& src, const Node& n, std::int64_t now);

// s048. A project's steps at a glance -- its DIRECT children that are todos,
// and how many are done ("1 of 3" on the tree row). Total 0: show nothing.
struct StepCount { int done = 0; int total = 0; };

// s048. The chip for a narrow row (the tree): the time is kept only where it
// is the news -- "Today 17:00", "Overdue 17:00" -- and dropped elsewhere
// ("Tomorrow 19:02" -> "Tomorrow", "Tue 09:00" -> "Tue"). The tooltip still
// has the whole date.
std::string compact_due(const std::string& chip);
StepCount step_count(const NodeSource& src, const NodeId& id);

// The due chip, short enough for a narrow pane:
//   due today            "Today" or "Today 17:00" (a bare date shows no time)
//   later today, passed  "Overdue 17:00"
//   tomorrow             "Tomorrow"
//   within six days      "Tue"
//   this year            "Oct 12"
//   another year         "Oct 12 2027"
//   a past day           "1d late", "12d late"   (calendar days, DST-safe)
std::string short_due(std::int64_t due, std::int64_t now);

// ── s043: the line under a view's title ────────────────────────────────────
// "4 to do  ·  1 late  ·  ~1h 30m" -- what the list adds up to, so a glance at
// the head of Today answers "how much is there" before any row is read. The
// minutes are the sum of the estimates that exist; unsized todos are counted
// and said ("2 not sized") rather than guessed at.
struct ViewSummary {
    int todo     = 0;   // not done
    int late     = 0;   // past due
    int today    = 0;   // due before the end of today, not late
    int flagged  = 0;
    int minutes  = 0;   // sum of estimates
    int unsized  = 0;   // todos with no estimate
};
ViewSummary summarize(const NodeSource& src, const std::vector<NodeId>& ids, std::int64_t now);
std::string summary_text(const ViewSummary& v);   // "Nothing to do" when empty

// s050. The desktop's accent colour as the settings portal gives it
// (org.freedesktop.appearance accent-color: three doubles, 0..1) -> "#rrggbb"
// for the stylesheet. "" when any part is out of range -- the portal's way of
// saying "no accent set" -- so the caller keeps its default.
std::string accent_css(double r, double g, double b);
inline constexpr const char* kDefaultAccent = "#3584e4";   // GNOME's blue

}  // namespace jot::core
