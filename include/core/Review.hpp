#pragma once
#include "core/Nodes.hpp"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Review -- road item 8 (s037): the weekly look at every project.
//
// OmniFocus's Review: each project wants a look every so often (a week by
// default). When it is due it shows in the Review list; you read it, fix what
// is stale, and Mark Reviewed -- which starts its clock again. Nothing else
// changes: a review is about YOUR attention, not the project's content, so it
// does not touch `modified` (MemoryNodes::set_task) and it is allowed on a
// protected note (like the Inbox mark).
//
// WHAT IS A PROJECT, for review. Not "any note with notes under it" (s031's
// word for the Status control) -- that would make every folder of reference
// notes, and every ancestor of every todo, want a weekly look. A project here
// is a node with a TODO DIRECTLY UNDER IT, or one you have said something
// about as a container (a Children order, or a state that is not Active).
// "Home" holding "Kitchen" holding todos: Kitchen is the project, Home is a
// folder.
//
// GTK-free; the Projects pane and the drawer draw what this answers.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// The rule above -- unless the node says otherwise (s037b, Task::mark): On is
// a project whatever is under it, Off never is.
bool is_project(const NodeSource& src, const NodeId& id);
// What the rule alone would say, ignoring the mark -- for "automatic" in the UI.
bool inferred_project(const NodeSource& src, const NodeId& id);
// Say it on purpose. Auto hands it back to the rule.
bool set_project_mark(NodeSource& src, const NodeId& id, ProjectMark m);

// How often: the node's own interval, or a week when it has none.
Repeat review_every(const Task& t);

// The day the next look is due (00:00 local): the last look -- or, never
// looked at, the day it was made -- plus the interval. Days, not seconds: a
// review is "this week", not "at 14:32".
std::int64_t next_review(const Node& n);

// Due a look by the end of today.
bool review_due(const Node& n, std::int64_t now);

// A project you would review: Active or On hold (a held project is exactly
// what a review should look at), and not inside a Completed or Dropped one.
bool reviewable(const NodeSource& src, const NodeId& id);

// Every reviewable project due a look by the end of today, the longest-waiting
// first; ties in document order.
std::vector<NodeId> review_list(const NodeSource& src, std::int64_t now);

// The soonest next review among the reviewable projects NOT due -- for "next
// review: Kitchen, Oct 11" when the list is empty. Empty id when there is none.
NodeId next_up(const NodeSource& src, std::int64_t now);

// Every project, by where it stands, in document order. Where it stands is
// the strongest reason in its chain (s031's rule, as availability() reads it):
// inside a completed project it is Completed, inside a dropped one Dropped,
// inside a held one On hold -- a list that called it Active would be offering
// work nothing else in jot offers.
struct ProjectsByState {
    std::vector<NodeId> active, on_hold, completed, dropped;
};
ProjectsByState projects_by_state(const NodeSource& src);
ProjectState    effective_state(const NodeSource& src, const NodeId& id);

// What is left in a project: todos anywhere under it that are not done or
// dropped, how many of those you can do now, and the next one in line.
struct ProjectCounts {
    std::size_t left      = 0;
    std::size_t available = 0;
    std::size_t total     = 0;   // every todo under it, finished or not
    NodeId      next;            // the first available todo, document order
};
ProjectCounts project_counts(const NodeSource& src, const NodeId& id, std::int64_t now);

// Start the clock again. Through set_task, so a jots folder writes it; does not
// change `modified`. False for an unknown id.
bool mark_reviewed(NodeSource& src, const NodeId& id, std::int64_t now);

// Set the interval. every == 0 (or a week) stores nothing: the default.
bool set_review_every(NodeSource& src, const NodeId& id, const Repeat& every);

// "Review today", "Review due since Mon 2026-10-02", "Next review Oct 11"...
// The words the pane and the drawer both use, so they cannot disagree.
std::string review_when(const Node& n, std::int64_t now);
// "Reviewed 2026-10-04" / "Never reviewed".
std::string reviewed_when(const Node& n);

}  // namespace jot::core
