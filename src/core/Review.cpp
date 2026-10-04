#include "core/Review.hpp"
#include "core/Tasks.hpp"

#include <algorithm>
#include <functional>

// core/Review.cpp -- road item 8. See the header for what a project is here
// and why a review does not touch `modified`.

namespace jot::core {
namespace {

// A day is a calendar day: DST makes one of them 23 h, so step by noon.
int days_between(std::int64_t from_day, std::int64_t to_day) {
    int n = 0;
    std::int64_t d = day_start(from_day);
    const std::int64_t end = day_start(to_day);
    while (d < end && n < 400) { d = day_start(d + 36 * 3600); ++n; }
    return n;
}

void walk(const NodeSource& src, const NodeId& parent,
          const std::function<void(const NodeId&)>& fn) {
    for (const auto& id : src.children(parent)) {
        fn(id);
        walk(src, id, fn);
    }
}

std::string lower_first(std::string s) {
    if (s == "Today" || s == "Yesterday") s[0] = static_cast<char>(s[0] - 'A' + 'a');
    return s;
}

}  // namespace

bool is_project(const NodeSource& src, const NodeId& id) {
    const Node* n = src.find(id);
    if (!n) return false;
    if (n->task.mark == ProjectMark::On)  return true;
    if (n->task.mark == ProjectMark::Off) return false;
    return inferred_project(src, id);
}

bool set_project_mark(NodeSource& src, const NodeId& id, ProjectMark m) {
    const Node* n = src.find(id);
    if (!n) return false;
    Task t = n->task;
    t.mark = m;
    return src.set_task(id, t);
}

bool inferred_project(const NodeSource& src, const NodeId& id) {
    const Node* n = src.find(id);
    if (!n) return false;
    if (n->task.status != Status::None) return true;
    if (n->task.project != ProjectState::Active) return true;
    for (const auto& k : src.children(id))
        if (const Node* c = src.find(k); c && c->task.is_task) return true;
    return false;
}

Repeat review_every(const Task& t) {
    if (t.review.on()) return t.review;
    Repeat r;
    r.every = 1;
    r.unit  = RepeatUnit::Week;
    return r;
}

std::int64_t next_review(const Node& n) {
    const std::int64_t base = n.task.reviewed ? n.task.reviewed : n.created;
    if (base == 0) return 0;                       // nothing to count from: due now
    return day_start(repeat_add(day_start(base), review_every(n.task)));
}

bool review_due(const Node& n, std::int64_t now) {
    return next_review(n) <= day_end(now);
}

bool reviewable(const NodeSource& src, const NodeId& id) {
    if (!is_project(src, id)) return false;
    const Node* n = src.find(id);
    const ProjectState own = project_state(*n);
    if (own != ProjectState::Active && own != ProjectState::OnHold) return false;
    for (const Node* p = src.find(n->parent_id); p; p = src.find(p->parent_id)) {
        const ProjectState s = project_state(*p);
        if (s == ProjectState::Completed || s == ProjectState::Dropped) return false;
    }
    return true;
}

std::vector<NodeId> review_list(const NodeSource& src, std::int64_t now) {
    std::vector<std::pair<std::int64_t, NodeId>> due;   // document order in, stable sort
    walk(src, "", [&](const NodeId& id) {
        if (!reviewable(src, id)) return;
        const Node* n = src.find(id);
        if (review_due(*n, now)) due.push_back({next_review(*n), id});
    });
    std::stable_sort(due.begin(), due.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<NodeId> out;
    out.reserve(due.size());
    for (auto& d : due) out.push_back(std::move(d.second));
    return out;
}

NodeId next_up(const NodeSource& src, std::int64_t now) {
    NodeId best;
    std::int64_t when = 0;
    walk(src, "", [&](const NodeId& id) {
        if (!reviewable(src, id)) return;
        const Node* n = src.find(id);
        if (review_due(*n, now)) return;
        const std::int64_t w = next_review(*n);
        if (best.empty() || w < when) { best = id; when = w; }
    });
    return best;
}

ProjectState effective_state(const NodeSource& src, const NodeId& id) {
    bool completed = false, dropped = false, held = false;
    for (const Node* n = src.find(id); n; n = src.find(n->parent_id)) {
        const ProjectState s = project_state(*n);
        completed = completed || s == ProjectState::Completed;
        dropped   = dropped   || s == ProjectState::Dropped;
        held      = held      || s == ProjectState::OnHold;
    }
    if (completed) return ProjectState::Completed;
    if (dropped)   return ProjectState::Dropped;
    if (held)      return ProjectState::OnHold;
    return ProjectState::Active;
}

ProjectsByState projects_by_state(const NodeSource& src) {
    ProjectsByState out;
    walk(src, "", [&](const NodeId& id) {
        if (!is_project(src, id)) return;
        switch (effective_state(src, id)) {
            case ProjectState::Active:    out.active.push_back(id);    break;
            case ProjectState::OnHold:    out.on_hold.push_back(id);   break;
            case ProjectState::Completed: out.completed.push_back(id); break;
            case ProjectState::Dropped:   out.dropped.push_back(id);   break;
        }
    });
    return out;
}

ProjectCounts project_counts(const NodeSource& src, const NodeId& id, std::int64_t now) {
    ProjectCounts c;
    walk(src, id, [&](const NodeId& k) {
        const Node* n = src.find(k);
        if (!n || !n->task.is_task) return;
        ++c.total;
        const Avail a = availability(src, k, now);
        if (a == Avail::Done || a == Avail::Dropped) return;
        ++c.left;
        if (a == Avail::Available) {
            ++c.available;
            if (c.next.empty()) c.next = k;
        }
    });
    return c;
}

bool mark_reviewed(NodeSource& src, const NodeId& id, std::int64_t now) {
    const Node* n = src.find(id);
    if (!n) return false;
    Task t = n->task;
    t.reviewed = now;
    return src.set_task(id, t);
}

bool set_review_every(NodeSource& src, const NodeId& id, const Repeat& every) {
    const Node* n = src.find(id);
    if (!n) return false;
    Task t = n->task;
    t.review = every;
    t.review.from_done = false;
    // A week IS the default: store nothing, so jot.json says only what you chose.
    if (t.review.every == 1 && t.review.unit == RepeatUnit::Week) t.review.every = 0;
    if (t.review.every < 0) t.review.every = 0;
    return src.set_task(id, t);
}

std::string review_when(const Node& n, std::int64_t now) {
    const std::int64_t next = next_review(n);
    const std::int64_t today = day_start(now);
    if (next == 0) return "Review today";
    if (next <= today) {
        if (next == today) return "Review today";
        return "Review due since " + lower_first(day_label(next, now));
    }
    const int d = days_between(today, next);
    if (d == 1) return "Next review tomorrow";
    if (d < 7)  return "Next review in " + std::to_string(d) + " days";
    // A future day: day_label names past weekdays only, so this is its dated form.
    return "Next review " + day_label(next, next + 8 * 24 * 3600);
}

std::string reviewed_when(const Node& n) {
    if (!n.task.reviewed) return "Never reviewed";
    return "Reviewed " + format_date(day_start(n.task.reviewed));
}

}  // namespace jot::core
