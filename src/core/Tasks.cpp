#include "core/Tasks.hpp"

#include <cctype>

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
        case Avail::Dropped:   return "Dropped";
        case Avail::OnHold:    return "On hold";
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

bool effective_flagged(const NodeSource& src, const NodeId& id) {
    if (const Node* n = src.find(id); n && n->task.flagged) return true;
    for (const auto& a : ancestors_of(src, id))
        if (const Node* p = src.find(a); p && p->task.flagged) return true;
    return false;
}

NodeId next_action(const NodeSource& src, const NodeId& parent) {
    for (const auto& kid : src.children(parent)) {
        const Node* k = src.find(kid);
        if (!k || !k->task.is_task) continue;   // a note is not a step
        if (!k->task.done) return kid;
    }
    return {};
}

// ── project state (s031) ────────────────────────────────────────────────────

ProjectState project_state(const Node& n) {
    if (n.task.is_task && n.task.done) return ProjectState::Completed;
    return n.task.project;
}

bool set_project_state(NodeSource& src, const NodeId& id, ProjectState s) {
    const Node* n = src.find(id);
    if (!n || n->protect) return false;
    Task t = n->task;
    if (t.is_task) {
        // A todo is finished by its tick and by nothing else.
        t.done    = (s == ProjectState::Completed);
        t.project = (s == ProjectState::Completed) ? ProjectState::Active : s;
    } else {
        t.project = s;
    }
    if (t == n->task) return true;   // already so: no write, no `modified` bump
    return src.set_task(id, t);
}

const char* project_state_name(ProjectState s) {
    switch (s) {
        case ProjectState::Active:    return "Active";
        case ProjectState::OnHold:    return "On hold";
        case ProjectState::Completed: return "Completed";
        case ProjectState::Dropped:   return "Dropped";
    }
    return "?";
}

NodeId stopped_by(const NodeSource& src, const NodeId& id) {
    if (const Node* n = src.find(id); n && project_state(*n) != ProjectState::Active) return id;
    for (const auto& a : ancestors_of(src, id))
        if (const Node* p = src.find(a); p && project_state(*p) != ProjectState::Active) return a;
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
    //
    // s031: the project word, on the node or any ancestor. Completed is Done
    // (above any other reason); then Dropped; then On hold. The strongest
    // reason in the chain wins, wherever in the chain it sits -- a dropped
    // project inside a held area is dropped, not merely paused.
    const auto chain = ancestors_of(src, id);
    bool dropped = n->task.project == ProjectState::Dropped;
    bool held    = n->task.project == ProjectState::OnHold;
    if (n->task.project == ProjectState::Completed) return Avail::Done;
    for (const auto& a : chain) {
        const Node* p = src.find(a);
        if (!p) continue;
        if (project_state(*p) == ProjectState::Completed) return Avail::Done;
        dropped = dropped || p->task.project == ProjectState::Dropped;
        held    = held || p->task.project == ProjectState::OnHold;
    }
    if (dropped) return Avail::Dropped;
    if (held)    return Avail::OnHold;

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
                       ((due != 0 && due <= today_ends) || effective_flagged(src, id));
                break;
            case Filter::Overdue:
                // Deliberately not gated on availability: a late task you
                // cannot start yet is the one you most need to see. (s031: On
                // hold still shows -- late is late; Dropped does not -- you
                // said you will not do it.)
                keep = a != Avail::Done && a != Avail::Dropped && due != 0 && due < now;
                break;
            case Filter::Scheduled:
                keep = a != Avail::Done && a != Avail::Dropped && due > today_ends;
                break;
            case Filter::Flagged:
                keep = a != Avail::Done && a != Avail::Dropped && effective_flagged(src, id);
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

// ── the Logbook (s032) ──────────────────────────────────────────────────────

std::vector<LogEntry> logbook(const NodeSource& src) {
    std::vector<LogEntry> out;
    std::vector<NodeId> stack;
    {
        const auto roots = src.children("");
        stack.assign(roots.rbegin(), roots.rend());
    }
    while (!stack.empty()) {                     // preorder: the undated tail reads in tree order
        const NodeId id = stack.back();
        stack.pop_back();
        if (const Node* n = src.find(id)) {
            const bool ticked = n->task.is_task && n->task.done;
            if (ticked || n->task.project == ProjectState::Completed)
                out.push_back({id, n->task.finished, ProjectState::Completed});
            else if (n->task.project == ProjectState::Dropped)
                out.push_back({id, n->task.finished, ProjectState::Dropped});
        }
        const auto kids = src.children(id);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    }
    for (const auto& r : src.history())                    // s033: repeats done
        out.push_back({r.id, r.when, ProjectState::Completed, true, r.title});
    std::stable_sort(out.begin(), out.end(), [](const LogEntry& a, const LogEntry& b) {
        if ((a.when == 0) != (b.when == 0)) return a.when != 0;   // dated first
        return a.when > b.when;                                   // newest first
    });
    return out;
}

std::vector<LogDay> group_by_day(const std::vector<LogEntry>& entries) {
    std::vector<LogDay> out;
    for (const auto& e : entries) {
        const std::int64_t d = e.when == 0 ? 0 : day_start(e.when);
        if (out.empty() || out.back().day != d) out.push_back(LogDay{d, {}});
        out.back().entries.push_back(e);
    }
    return out;
}

std::string day_label(std::int64_t day, std::int64_t now) {
    if (day == 0) return "No date recorded";
    const std::int64_t today = day_start(now);
    const std::int64_t d     = day_start(day);
    // Days by calendar, not by 86400 seconds: a DST change makes one day 23 h.
    const std::int64_t yesterday = day_start(today - 12 * 3600);
    if (d == today)     return "Today";
    if (d == yesterday) return "Yesterday";
    const std::tm tm = local_of(d);
    const std::tm tn = local_of(today);
    char buf[48];
    if (d < today && today - d < 7 * 24 * 3600 - 3600)
        std::strftime(buf, sizeof buf, "%A", &tm);                 // "Wednesday"
    else if (tm.tm_year == tn.tm_year)
        std::strftime(buf, sizeof buf, "%a %e %b", &tm);           // "Wed 23 Sep"
    else
        std::strftime(buf, sizeof buf, "%a %e %b %Y", &tm);
    std::string out = buf;
    if (auto p = out.find("  "); p != std::string::npos) out.erase(p, 1);   // %e pads " 3"
    return out;
}

std::string format_clock(std::int64_t when) {
    if (when == 0) return {};
    const std::tm tm = local_of(when);
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M", &tm);
    return buf;
}

// ── quick picks (s033b) ─────────────────────────────────────────────────────

std::string quick_date_text(QuickDate q, std::int64_t now, std::int64_t due) {
    std::tm tm = local_of(q == QuickDate::BeforeDue ? due : now);
    if (q == QuickDate::BeforeDue && due == 0) return {};
    int add = 0;
    switch (q) {
        case QuickDate::Today:       add = 0; break;
        case QuickDate::Tomorrow:    add = 1; break;
        case QuickDate::NextWeek:    add = 7; break;
        case QuickDate::ThisWeekend: add = tm.tm_wday == 0 ? 0 : (6 - tm.tm_wday); break;
        case QuickDate::NextMonday:  add = ((8 - tm.tm_wday) % 7) == 0 ? 7 : (8 - tm.tm_wday) % 7; break;
        case QuickDate::BeforeDue:   add = -2; break;
    }
    tm.tm_mday += add;                 // calendar days through mktime: DST-safe
    tm.tm_hour = 12; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_isdst = -1;
    const std::time_t t = std::mktime(&tm);
    std::tm out{};
    localtime_r(&t, &out);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &out);
    return buf;
}

// ── estimates (s040) ────────────────────────────────────────────────────────
int parse_estimate(const std::string& text) {
    std::string t;
    for (char c : text)
        if (c != ' ' && c != '\t') t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t.empty()) return 0;
    double total = 0;
    bool any = false, hours_seen = false;
    std::size_t i = 0;
    while (i < t.size()) {
        std::size_t j = i;
        while (j < t.size() && (std::isdigit(static_cast<unsigned char>(t[j])) || t[j] == '.')) ++j;
        if (j == i) return -1;
        double v = 0;
        try { v = std::stod(t.substr(i, j - i)); } catch (...) { return -1; }
        std::size_t k = j;
        while (k < t.size() && std::isalpha(static_cast<unsigned char>(t[k]))) ++k;
        const std::string unit = t.substr(j, k - j);
        if (unit == "h" || unit == "hr" || unit == "hrs" || unit == "hour" || unit == "hours") {
            if (hours_seen) return -1;
            hours_seen = true;
            total += v * 60;
        } else if (unit.empty() || unit == "m" || unit == "min" || unit == "mins" ||
                   unit == "minute" || unit == "minutes") {
            total += v;   // "1h30": the bare number after hours is minutes
        } else {
            return -1;
        }
        any = true;
        i = k;
    }
    if (!any || total < 0 || total > 100000) return -1;
    return static_cast<int>(total + 0.5);
}

std::string format_estimate(int minutes) {
    if (minutes <= 0) return {};
    const int h = minutes / 60, m = minutes % 60;
    if (h == 0) return std::to_string(m) + "m";
    if (m == 0) return std::to_string(h) + "h";
    return std::to_string(h) + "h " + std::to_string(m) + "m";
}

}  // namespace jot::core
