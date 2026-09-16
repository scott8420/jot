#include "core/Projection.hpp"
#include "core/Markdown.hpp"

#include <cstdio>

// Projection.cpp -- the decision, and nothing but. No libecal here, no GTK, no
// clock: `now` arrives as an argument so the selftest can stand at any instant
// and so every row in one projection is consistent with every other.

namespace jot::core {
namespace {

std::string titled(const Node* n) {
    if (!n) return {};
    return n->title.empty() ? std::string("Untitled") : n->title;
}

// The description line. Availability first, because it is the thing the desktop
// cannot work out for itself and the thing a row on a day most needs qualifying
// by: a blocked task on today's card should not read as a thing you can pick up.
//
// Then the WHY -- the parent's title and the first line of its note. That is
// the sentence OmniFocus cannot put on a desktop pill, because an OmniFocus
// task's note is a scratch field; a jot task's parent IS a note.
std::string detail_for(const NodeSource& src, const Node& n, Avail a) {
    std::string out = avail_name(a);
    if (n.task.flagged) out += "  \u00b7  Flagged";

    const Node* parent = n.parent_id.empty() ? nullptr : src.find(n.parent_id);
    if (parent) {
        out += "\n\n" + titled(parent);
        const std::string why = first_prose_line(parent->body);
        if (!why.empty()) out += "\n" + why;
    }
    return out;
}

}  // namespace

const char* projection_prefix() { return "jot-"; }

std::string projection_uid(const NodeId& id) {
    return std::string(projection_prefix()) + id;
}

std::vector<Projected> project(const NodeSource& src, const TaskIndex& tasks,
                               std::int64_t now) {
    std::vector<Projected> out;
    out.reserve(tasks.count());

    for (const NodeId& id : tasks.tasks()) {
        const Node* n = src.find(id);
        if (!n || !n->task.is_task) continue;   // the index holds membership; trust but check
        if (n->task.done) continue;             // the card answers "what is on", not "what was"

        const std::int64_t due = effective_due(src, id);
        if (due == 0) continue;                 // no day, nothing to put on the grid

        Projected p;
        p.id      = id;
        p.uid     = projection_uid(id);
        p.summary = titled(n);
        p.due     = due;
        p.overdue = due < now;
        p.flagged = n->task.flagged;

        // A bare `YYYY-MM-DD` due parses to the END of that day (Tasks.hpp:
        // a thing due Tuesday is not late at 00:01 Tuesday). That is exactly
        // how we recognise one here -- which means the two live and die
        // together, and a change to day_end() shows up as a failing check.
        p.all_day = (due == day_end(due));
        p.start   = p.all_day ? day_start(due) : due;

        p.detail = detail_for(src, *n, availability(src, id, now));
        out.push_back(std::move(p));
    }
    return out;
}

// FNV-1a over every field that reaches the desktop. Not a hash of the model:
// a body edit, a rename of an unrelated note, a tick on an undated todo all
// leave this alone, and that is the point -- it changes when and only when the
// desktop would look different.
std::string projection_signature(const std::vector<Projected>& items) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    auto mix_s = [&](const std::string& s) { mix(s.data(), s.size()); mix("\x1f", 1); };

    for (const auto& p : items) {
        mix_s(p.uid);
        mix_s(p.summary);
        mix_s(p.detail);
        mix(&p.start, sizeof p.start);
        mix(&p.due,   sizeof p.due);
        const unsigned char flags = static_cast<unsigned char>(
            (p.all_day ? 1 : 0) | (p.overdue ? 2 : 0) | (p.flagged ? 4 : 0));
        mix(&flags, 1);
        mix("\x1e", 1);
    }

    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

}  // namespace jot::core
