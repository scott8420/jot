#pragma once
#include "core/Nodes.hpp"
#include "core/RowLook.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Timeline -- J5 (s059, Scott's Oct 6 design): a running calendar, the
// work hanging down from it in clumps, one clump per day.
//
// What lands on which day -- each rule written down because each could have
// gone another way:
//
//   project  its OWN due; else its own defer (it "starts" then); else Someday.
//            Completed: the day it was finished. Dropped: left out.
//   todo     its OWN due; else -- if a project or todo above it is on the
//            timeline -- it rides in that one's STEPS (right-click opens them),
//            not on a day of its own; else its own defer ("starts"); else
//            Someday. Ticked: the day it was ticked (the past is the
//            Logbook, along the same line). Dropped: left out.
//   note     the day it was made. A note is not work; its day is where it
//            came into the world, which is what you look for along a line.
//
//   Someday  undated work -- the projects and todos with no day anywhere. Off
//            by default (Scott: "a filter that brings in the someday items").
//
// A day's clump lists projects first, then todos (late first), then notes,
// each in document order. GTK-free; `now` is passed in, like everywhere in
// core. The drawing is TimelinePane's; the placement arithmetic it needs
// (clumps stacked down without overlapping, the thumbnail's box) is here, so
// it can be selftested.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

enum class TlKind { Project, Todo, Note };
enum class TlWhy  { Due, Starts, Done, Made, Someday };

// The colour a dot wears -- RowState's meaning, plus a note's quiet grey.
enum class TlTone { Late, Today, Flagged, Available, Waiting, Done, Note };

struct TlItem {
    NodeId       id;
    TlKind       kind = TlKind::Todo;
    TlWhy        why  = TlWhy::Due;
    TlTone       tone = TlTone::Available;
    std::int64_t day  = 0;            // 00:00 local; 0 for Someday
    int          estimate = 0;        // minutes; the dot's size
    std::string  title;
    std::vector<NodeId> steps;        // undated work under it, not done, document order
};

struct TlDay {
    std::int64_t        day = 0;
    std::vector<TlItem> items;
};

// A goal with feeders (s058), drawn as a thread from today to its due, with a
// bead on each day a feeder comes round before then.
struct TlBead {
    NodeId       feeder;
    std::int64_t day = 0;
    bool         slipped = false;
};
struct TlThread {
    NodeId              goal;
    std::string         title;
    std::int64_t        due_day = 0;
    bool                slipped = false;
    std::vector<TlBead> beads;        // by day
};

struct TlShow {
    bool projects = true;
    bool todos    = true;
    bool notes    = true;
    bool someday  = false;
};

struct Timeline {
    std::vector<TlDay>    days;       // only days with something on them, in order
    std::vector<TlItem>   someday;    // empty unless asked for
    std::vector<TlThread> threads;
    std::int64_t first = 0;           // the span: two weeks before today at least ...
    std::int64_t last  = 0;           // ... eight weeks after it at least, and every item
    std::size_t  dated = 0;           // items on days
    int          late  = 0;           // of them, late
};

Timeline build_timeline(const NodeSource& src, const TlShow& show, std::int64_t now);

// Days from `first` to `day`, both 00:00 local -- DST-safe (rounds).
int day_index(std::int64_t first, std::int64_t day);
// The 00:00 `n` days after `first`.
std::int64_t day_at(std::int64_t first, int n);

// ── s060: clusters along the line ──────────────────────────────────────────
// The same timeline, regrouped into LANES (Scott's bubbles: "a relationship
// of tasks or notes"). Each lane runs the whole span; a day's card sits in
// the lane its things belong to.
//
//   Day      one lane, no name -- the plain timeline (s059)
//   Place    one lane per #at/ place (s056's runs: #at/town/bank is Town);
//            a thing with two places is in both; "No place" last
//   Purpose  what the work is FOR: the goal it feeds (s058), else the
//            project it is in (or is), else a goal it is; "Loose ends" last
//
// Lanes come in order of their soonest open dated thing (late first), the
// catch-all lane last. A lane's line: "4 things · ~1h 10m · 1 late".
enum class TlGroup { Day, Place, Purpose };

struct TlLane {
    std::string           key;       // "" = the catch-all (or the one Day lane)
    std::string           title;     // "Town", "Taxes 2027", "No place"; "" for Day
    std::string           line;
    std::vector<TlDay>    days;
    std::vector<TlItem>   someday;
};

std::vector<TlLane> timeline_lanes(const NodeSource& src, const Timeline& t, TlGroup g,
                                   std::int64_t now);
const char* tl_group_word(TlGroup g);   // "day" "place" "purpose"

// The head line: "34 on the line · 5 late · 9 in the next 7 days".
std::string timeline_summary(const Timeline& t, std::int64_t now);

// Every item (and step) that `query` (Find's language, s038b) matches, in
// timeline order: day by day, then Someday. A step that matches stands for
// itself; the pane lights the row that carries it.
std::vector<NodeId> timeline_matches(const NodeSource& src, const Timeline& t,
                                     const std::string& query, std::int64_t now);

// ── placement ───────────────────────────────────────────────────────────────
// Boxes hang from the strip. Each wants a horizontal span [x0, x1) and a
// height; it goes as HIGH as it can without touching any earlier box whose
// span overlaps its own (gaps included). Returns each box's top, in order.
// First-fit, so a crowded stretch staggers downward and a quiet one stays up.
struct TlSpan { double x0 = 0, x1 = 0, h = 0; };
// `pinned` (optional, -1 = free): a box that must stay at that top -- the
// clump just opened, so it does not jump away from the hand; the rest make
// room around it.
std::vector<double> stack_down(const std::vector<TlSpan>& boxes, double hgap, double vgap,
                               const std::vector<double>& pinned = {});

// The thumbnail: content of size (cw, ch) drawn in a (tw, th) inset; the view
// (vx, vy, vw, vh) in content coordinates becomes this box in the inset.
struct TlRect { double x = 0, y = 0, w = 0, h = 0; };
TlRect thumb_box(double cw, double ch, double tw, double th, const TlRect& view);
// And back: a point in the inset -> the view's top-left that centres there,
// kept inside the content.
void thumb_to_view(double cw, double ch, double tw, double th, double px, double py,
                   double vw, double vh, double& vx, double& vy);

const char* tl_tone_word(TlTone t);   // "late", "today", ... for the log
// The tone of any node -- for a step drawn when its clump opens.
TlTone tl_tone(const NodeSource& src, const NodeId& id, std::int64_t now);

}  // namespace jot::core
