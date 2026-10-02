#pragma once
#include "core/Nodes.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Tasks -- what is actually available to do, and the index that finds it.
//
// D2 made a todo a NODE (see ARC). This file is the half that makes that worth
// having: given the tree, the done flags, the defer dates and each parent's
// Sequential/Parallel status, it answers "what should I do now" as a
// COMPUTATION rather than as a list you maintain by hand.
//
// AVAILABILITY IS DERIVED, NEVER STORED. It is a pure function of (parent
// status, sibling order, done, defer) and therefore a pure function under the
// selftest, where a wrong answer is visible. A stored availability flag is a
// cache, a cache goes stale, and the failure mode of a stale availability flag
// is the worst one this app can have: A TASK WRONGLY HIDDEN IS A TASK YOU DO
// NOT DO, and nothing on screen would ever tell you it was missing.
//
// So nothing here caches an answer. `TaskIndex` caches only the SET of nodes
// that are todos, in document order -- membership, not verdicts -- and every
// query re-derives the verdict from the source it is handed.
//
// GTK-free, like everything under core/.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// Why a task is or is not actionable right now. Ordered by how early the test
// short-circuits, which is also roughly "how far from doable".
enum class Avail {
    NotTask,     // not a todo at all
    Done,        // ticked, or inside a finished parent task / completed project
    Dropped,     // s031: it, or a project it is in, was dropped
    OnHold,      // s031: it, or a project it is in, is on hold
    Deferred,    // its own defer date, or an ancestor's, is in the future
    Blocked,     // an ancestor is Sequential and something earlier is unfinished
    Available    // do it now
};

const char* avail_name(Avail a);       // "Available", "Blocked", ... for the UI and the log

// ── inheritance ─────────────────────────────────────────────────────────────
// Both dates inherit DOWN the tree, in opposite directions, and the asymmetry
// is the point:
//
//   defer -- the LATEST wins. If the project is deferred to next month, none of
//            its steps is available before then, however early their own defer.
//   due   -- the EARLIEST non-zero wins. If the project is due Friday, a step
//            inside it is due Friday whatever it says itself.
//
// Both walk ancestors including the node itself. A note in the chain still
// carries dates: a plain note is the natural project container (see Status).
std::int64_t effective_defer(const NodeSource& src, const NodeId& id);
std::int64_t effective_due(const NodeSource& src, const NodeId& id);

// ── the engine ──────────────────────────────────────────────────────────────
// `now` is passed in rather than read from the clock so the selftest can stand
// at any instant and so one query's answers are all consistent with each other.
//
// The Sequential rule: for every ancestor P of `id` (and for `id`'s own parent),
// if P is Sequential then the child of P on the path down to `id` must be P's
// FIRST INCOMPLETE TASK CHILD. Notes among the children are skipped rather than
// blocking -- a reference note sitting in the middle of a sequence is not a step
// and cannot be "done", so treating it as a blocker would stop the sequence dead
// with nothing to tick.
Avail availability(const NodeSource& src, const NodeId& id, std::int64_t now);

// The first incomplete TASK child of a parent; empty if it has none. What a
// Sequential parent's next action is, and what the drawer shows on a parent.
NodeId next_action(const NodeSource& src, const NodeId& parent);

// ── project state (s031) ────────────────────────────────────────────────────
// Where a node stands AS A PROJECT. One reading for both kinds of container:
// a done todo reads Completed, so the drawer and the menu never show a ticked
// project as Active.
ProjectState project_state(const Node& n);

// The one writer. On a TODO, Completed is `done` (and any other state unticks
// it); on a note it is the stored word. So a project has exactly one way to be
// finished, and "Completed" in the menu and the tick in the tree cannot
// disagree. Refused (false) on an unknown or protected node.
bool set_project_state(NodeSource& src, const NodeId& id, ProjectState s);

const char* project_state_name(ProjectState s);   // "Active", "On hold", ...

// The nearest node -- `id` itself or an ancestor -- whose project state is not
// Active (Completed counts, including a done todo above `id`). Empty when the
// whole chain is active. What the drawer names when it says WHY.
NodeId stopped_by(const NodeSource& src, const NodeId& id);

// ── dates, at the surface's edge ────────────────────────────────────────────
// Stored as epoch seconds; typed and shown as local text. Both directions live
// here, adjacent, so a parse cannot skew from its format (CANON: encode and
// decode adjacent).
//
// Accepted: "" (means none), "YYYY-MM-DD", "YYYY-MM-DD HH:MM", "today",
// "tomorrow". A bare date means the END of that day for a due (a thing due
// Tuesday is not late at 00:01 Tuesday) and the START of it for a defer (a
// thing deferred to Tuesday is available when Tuesday begins). That asymmetry
// is invisible in the text and wrong in either direction if collapsed, which is
// why `kind` is a parameter rather than a default.
enum class DateKind { Due, Defer };

std::string  format_date(std::int64_t when);                      // "" when 0
std::int64_t parse_date(const std::string& text, DateKind kind,
                        std::int64_t now);                        // 0 when empty OR unparseable
bool         date_parses(const std::string& text);                // "" counts as parsing

// s033b. The quick picks beside the date fields' calendars. Each answers a
// DAY as "YYYY-MM-DD" text -- the field's own language, so a pick goes through
// the same parse as typing and the due/defer end/start-of-day rule still holds.
// "" when the pick does not apply (BeforeDue with no due date).
//   ThisWeekend  the coming Saturday; today if today is Saturday or Sunday
//   NextMonday   the Monday after today (a week on, if today is Monday)
//   BeforeDue    two days before the due date -- "show it in time to do it"
enum class QuickDate { Today, Tomorrow, NextWeek, ThisWeekend, NextMonday, BeforeDue };
std::string quick_date_text(QuickDate q, std::int64_t now, std::int64_t due);

std::int64_t day_start(std::int64_t when);   // 00:00:00 local, same day
std::int64_t day_end(std::int64_t when);     // 23:59:59 local, same day

// ── the queries ─────────────────────────────────────────────────────────────
enum class Filter {
    All,         // every todo, whatever its state
    Available,   // actionable right now
    Today,       // available AND (due by end of today OR flagged)
    Overdue,     // due before now and not done -- shown even when blocked,
                 // because a late task you cannot start is the one you most
                 // need to see
    Scheduled,   // has a due date in the future
    Flagged,     // flagged and not done
    Done
};

// ── TaskIndex -- the LinkIndex shape, again ─────────────────────────────────
// One walk at load, incremental after, NOTHING STORED but membership. It holds
// the ids that are todos, in document (preorder) order, so a query is a filter
// over a short vector instead of a walk of the whole tree.
//
// What it deliberately does NOT hold is any verdict: no availability, no
// bucket, no due-date copy. Membership can be checked against the model in one
// comparison (`rebuild()` must agree with the incremental path -- that is the
// selftest check that earns this class its keep), while a cached verdict could
// disagree with the model in a way nothing observable would reveal.
class TaskIndex {
public:
    // Walk the tree from the roots. Called at load and after a wholesale
    // reload. Idempotent.
    void rebuild(const NodeSource& src);

    // One node's task fields changed. Membership is the only thing that can
    // move, and a membership change cannot be placed locally -- document order
    // is a property of the whole tree -- so a change re-walks and a non-change
    // costs one set lookup. A structural change (create, move, delete) calls
    // rebuild(): a move changes the ORDER of ids that are all still tasks, and
    // there is nothing cheaper than the walk that produced the order.
    void update(const NodeSource& src, const NodeId& id);

    void erase(const NodeId& id);

    const std::vector<NodeId>& tasks() const { return m_order; }
    bool        holds(const NodeId& id) const { return m_set.count(id) != 0; }
    std::size_t count() const { return m_order.size(); }

    // Derived on every call, in document order. `now` is the caller's instant.
    std::vector<NodeId> query(const NodeSource& src, Filter f, std::int64_t now) const;

private:
    std::vector<NodeId>          m_order;   // document order
    std::unordered_set<NodeId>   m_set;     // membership, for O(1) holds()
};

// ── grouping: the report jot can write and OmniFocus cannot ─────────────────
// Today groups BY PARENT, because the parent's note is the WHY. An OmniFocus
// task's note is a scratch field nobody uses; a jot task IS a note, and its
// parent is a note too, so the group header has real prose behind it.
//
// Parents appear in the order their first task appears, and tasks keep the
// order they were given. A task at the top level groups under an empty parent
// id -- unfiled is a STATE, not a place, so there is no Inbox to route it to.
struct TaskGroup {
    NodeId              parent;   // "" for top-level todos
    std::vector<NodeId> tasks;
};

std::vector<TaskGroup> group_by_parent(const NodeSource& src,
                                       const std::vector<NodeId>& ids);

// ── the Logbook (s032) ──────────────────────────────────────────────────────
// What got done, newest first. An entry is a node that is finished BY ITSELF:
// a ticked todo, or a project marked Completed or Dropped. A todo that only
// counts as done because its project was completed is NOT listed again -- the
// project's own line says it.
//
// The time is Task::finished, stamped by the store. Items finished before
// s032 have no time and sort last, under their own heading: a made-up date
// (`modified`, say) would put them on a day they were not done.
//
// A whole-tree walk, not the TaskIndex: a completed project is very often a
// plain note, and the index only holds todos.
struct LogEntry {
    NodeId       id;
    std::int64_t when = 0;                          // 0 == undated
    ProjectState kind = ProjectState::Completed;    // Completed (incl. a tick) or Dropped
    // s033: one done occurrence of a repeating todo, from the history. The
    // note has rolled on, so `title` is the record's own copy and the row has
    // no tick to take back.
    bool         repeat = false;
    std::string  title;
};

std::vector<LogEntry> logbook(const NodeSource& src);

// Entries split by LOCAL day, in the order given. `day` is that day's
// day_start(); 0 for the undated tail.
struct LogDay {
    std::int64_t          day = 0;
    std::vector<LogEntry> entries;
};
std::vector<LogDay> group_by_day(const std::vector<LogEntry>& entries);

// "Today", "Yesterday", a weekday name within the last week, else
// "Wed 23 Sep" (with the year when it is not this year). "No date recorded"
// for 0. `now` is the caller's instant, like everywhere in this file.
std::string day_label(std::int64_t day, std::int64_t now);
std::string format_clock(std::int64_t when);    // "14:32"; "" when 0

}  // namespace jot::core
