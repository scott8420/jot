#include "core/Tasks.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <unordered_map>

// core/Tasks.cpp -- the GTD engine. Every function here is pure with respect to
// the NodeSource it is handed: it reads, it decides, it writes nothing.

namespace jot::core {

namespace {

// Ancestors of `id`, nearest first, NOT including `id` itself. Guarded against
// a cycle by a depth cap rather than a visited set: the model already refuses
// to create one (can_move), so this is a belt on braces, and a cap costs
// nothing on the sane path.
std::vector<NodeId> ancestors_of(const NodeSource& src, const NodeId& id) {
    std::vector<NodeId> out;
    const Node* n = src.find(id);
    int guard = 0;
    while (n && !n->parent_id.empty() && ++guard < 1000) {
        out.push_back(n->parent_id);
        n = src.find(n->parent_id);
    }
    return out;
}

void walk(const NodeSource& src, const NodeId& parent, std::vector<NodeId>& out) {
    for (const auto& id : src.children(parent)) {
        out.push_back(id);
        walk(src, id, out);
    }
}

std::tm local_of(std::int64_t when) {
    std::tm tm{};
    const std::time_t t = static_cast<std::time_t>(when);
    localtime_r(&t, &tm);
    return tm;
}

}  // namespace

const char* avail_name(Avail a) {
    switch (a) {
        case Avail::NotTask:   return "Not a todo";
        case Avail::Done:      return "Done";
        case Avail::Deferred:  return "Deferred";
        case Avail::Blocked:   return "Blocked";
        case Avail::Available: return "Available";
    }
    return "?";
}

// ── inheritance ─────────────────────────────────────────────────────────────

std::int64_t effective_defer(const NodeSource& src, const NodeId& id) {
    std::int64_t latest = 0;
    if (const Node* n = src.find(id)) latest = n->task.defer;
    for (const auto& a : ancestors_of(src, id))
        if (const Node* p = src.find(a)) latest = std::max(latest, p->task.defer);
    return latest;
}

std::int64_t effective_due(const NodeSource& src, const NodeId& id) {
    std::int64_t soonest = 0;
    auto consider = [&](std::int64_t d) {
        if (d == 0) return;
        if (soonest == 0 || d < soonest) soonest = d;
    };
    if (const Node* n = src.find(id)) consider(n->task.due);
    for (const auto& a : ancestors_of(src, id))
        if (const Node* p = src.find(a)) consider(p->task.due);
    return soonest;
}

NodeId next_action(const NodeSource& src, const NodeId& parent) {
    for (const auto& kid : src.children(parent)) {
        const Node* k = src.find(kid);
        if (!k || !k->task.is_task) continue;   // a note is not a step
        if (!k->task.done) return kid;
    }
    return {};
}

// ── the engine ──────────────────────────────────────────────────────────────

Avail availability(const NodeSource& src, const NodeId& id, std::int64_t now) {
    const Node* n = src.find(id);
    if (!n || !n->task.is_task) return Avail::NotTask;
    if (n->task.done) return Avail::Done;

    // A finished parent TASK takes its unfinished children with it. The
    // alternative -- leaving steps of a completed project available forever --
    // is the one way a wrong answer here is loud rather than silent, and it is
    // still wrong.
    for (const auto& a : ancestors_of(src, id)) {
        const Node* p = src.find(a);
        if (p && p->task.is_task && p->task.done) return Avail::Done;
    }

    if (effective_defer(src, id) > now) return Avail::Deferred;

    // The Sequential rule, walked from the node upward: at each step, the child
    // we came from must be its parent's first incomplete task child.
    NodeId child = id;
    int guard = 0;
    while (++guard < 1000) {
        const Node* c = src.find(child);
        if (!c || c->parent_id.empty()) break;
        const Node* p = src.find(c->parent_id);
        if (!p) break;
        if (p->task.status == Status::Sequential && next_action(src, c->parent_id) != child)
            return Avail::Blocked;
        child = c->parent_id;
    }
    return Avail::Available;
}

// ── dates ───────────────────────────────────────────────────────────────────

std::int64_t day_start(std::int64_t when) {
    std::tm tm = local_of(when);
    tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm));
}

std::int64_t day_end(std::int64_t when) {
    std::tm tm = local_of(when);
    tm.tm_hour = 23; tm.tm_min = 59; tm.tm_sec = 59; tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm));
}

std::string format_date(std::int64_t when) {
    if (when == 0) return {};
    const std::tm tm = local_of(when);
    char buf[32];
    // A time of exactly midnight or of one second to midnight is what a bare
    // date round-trips to, so it is shown as a bare date. Anything else had a
    // clock time typed into it and keeps one.
    const bool bare = (tm.tm_hour == 0 && tm.tm_min == 0 && tm.tm_sec == 0) ||
                      (tm.tm_hour == 23 && tm.tm_min == 59 && tm.tm_sec == 59);
    if (bare) std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    else      std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

namespace {

std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// Split the accepted shapes without a regex: a regex here would be one more
// thing to be subtly wrong about and nothing to gain.
bool split_date(const std::string& t, int& y, int& m, int& d, int& hh, int& mm,
                bool& has_time) {
    y = m = d = hh = mm = 0;
    has_time = false;
    int n = 0;
    if (std::sscanf(t.c_str(), "%d-%d-%d %d:%d%n", &y, &m, &d, &hh, &mm, &n) == 5 &&
        n == static_cast<int>(t.size())) {
        has_time = true;
    } else if (std::sscanf(t.c_str(), "%d-%d-%d%n", &y, &m, &d, &n) == 3 &&
               n == static_cast<int>(t.size())) {
        has_time = false;
    } else {
        return false;
    }
    if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    return true;
}

}  // namespace

std::int64_t parse_date(const std::string& text, DateKind kind, std::int64_t now) {
    const std::string t = trimmed(text);
    if (t.empty()) return 0;

    auto bare = [&](std::int64_t day) {
        return kind == DateKind::Due ? day_end(day) : day_start(day);
    };
    if (t == "today")    return bare(now);
    if (t == "tomorrow") return bare(now + 24 * 3600);

    int y, m, d, hh, mm; bool has_time;
    if (!split_date(t, y, m, d, hh, mm, has_time)) return 0;

    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d;
    tm.tm_hour = has_time ? hh : (kind == DateKind::Due ? 23 : 0);
    tm.tm_min  = has_time ? mm : (kind == DateKind::Due ? 59 : 0);
    tm.tm_sec  = has_time ? 0  : (kind == DateKind::Due ? 59 : 0);
    tm.tm_isdst = -1;
    const std::time_t out = std::mktime(&tm);
    if (out == static_cast<std::time_t>(-1)) return 0;
    // mktime NORMALISES, so 2026-02-31 comes back as March 3rd rather than as a
    // refusal. Silently accepting a date the user did not type is worse than
    // refusing it, so the normalised fields are compared against what was typed.
    std::tm back = local_of(static_cast<std::int64_t>(out));
    if (back.tm_year != y - 1900 || back.tm_mon != m - 1 || back.tm_mday != d) return 0;
    return static_cast<std::int64_t>(out);
}

bool date_parses(const std::string& text) {
    const std::string t = trimmed(text);
    if (t.empty() || t == "today" || t == "tomorrow") return true;
    int y, m, d, hh, mm; bool has_time;
    if (!split_date(t, y, m, d, hh, mm, has_time)) return false;
    return parse_date(t, DateKind::Due, 0) != 0;
}

// ── TaskIndex ───────────────────────────────────────────────────────────────

void TaskIndex::rebuild(const NodeSource& src) {
    m_order.clear();
    m_set.clear();
    std::vector<NodeId> all;
    walk(src, NodeId{}, all);
    for (const auto& id : all) {
        const Node* n = src.find(id);
        if (n && n->task.is_task) { m_order.push_back(id); m_set.insert(id); }
    }
}

void TaskIndex::update(const NodeSource& src, const NodeId& id) {
    const Node* n = src.find(id);
    const bool should = n && n->task.is_task;
    if (should == holds(id)) return;   // only membership can move; nothing did
    rebuild(src);                      // document order is a whole-tree property
}

void TaskIndex::erase(const NodeId& id) {
    if (!m_set.erase(id)) return;
    m_order.erase(std::remove(m_order.begin(), m_order.end(), id), m_order.end());
}

std::vector<NodeId> TaskIndex::query(const NodeSource& src, Filter f,
                                     std::int64_t now) const {
    std::vector<NodeId> out;
    const std::int64_t today_ends = day_end(now);

    for (const auto& id : m_order) {
        const Node* n = src.find(id);
        if (!n) continue;                       // gone from under us; erase() is late
        const Avail a   = availability(src, id, now);
        const std::int64_t due = effective_due(src, id);

        bool keep = false;
        switch (f) {
            case Filter::All:       keep = true; break;
            case Filter::Done:      keep = a == Avail::Done; break;
            case Filter::Available: keep = a == Avail::Available; break;
            case Filter::Today:
                keep = a == Avail::Available &&
                       ((due != 0 && due <= today_ends) || n->task.flagged);
                break;
            case Filter::Overdue:
                // Deliberately not gated on availability: a late task you
                // cannot start yet is the one you most need to see.
                keep = a != Avail::Done && due != 0 && due < now;
                break;
            case Filter::Scheduled:
                keep = a != Avail::Done && due > today_ends;
                break;
            case Filter::Flagged:
                keep = a != Avail::Done && n->task.flagged;
                break;
        }
        if (keep) out.push_back(id);
    }
    return out;
}

std::vector<TaskGroup> group_by_parent(const NodeSource& src,
                                       const std::vector<NodeId>& ids) {
    std::vector<TaskGroup> out;
    std::unordered_map<NodeId, std::size_t> at;   // parent -> index in out
    for (const auto& id : ids) {
        const Node* n = src.find(id);
        if (!n) continue;
        const NodeId parent = n->parent_id;
        auto it = at.find(parent);
        if (it == at.end()) {
            at.emplace(parent, out.size());
            out.push_back(TaskGroup{parent, {id}});
        } else {
            out[it->second].tasks.push_back(id);
        }
    }
    return out;
}

}  // namespace jot::core
