#include "core/Routine.hpp"
#include "core/Repeat.hpp"
#include "core/Tasks.hpp"

#include <algorithm>
#include <ctime>

namespace jot::core {
namespace {

std::tm local(std::int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm lt{};
    localtime_r(&tt, &lt);
    return lt;
}

int days_between(std::int64_t a, std::int64_t b) {
    std::tm x = local(a), y = local(b);
    x.tm_hour = y.tm_hour = 12;
    x.tm_min = y.tm_min = x.tm_sec = y.tm_sec = 0;
    x.tm_isdst = y.tm_isdst = -1;
    const double secs = std::difftime(std::mktime(&y), std::mktime(&x));
    return static_cast<int>(secs >= 0 ? (secs + 43200) / 86400 : (secs - 43200) / 86400);
}

// "Tue 29 Sep"
std::string short_day(std::int64_t when) {
    const std::tm d = local(when);
    char wd[16], mon[16];
    std::strftime(wd, sizeof wd, "%a", &d);
    std::strftime(mon, sizeof mon, "%b", &d);
    return std::string(wd) + " " + std::to_string(d.tm_mday) + " " + mon;
}

}  // namespace

const char* punct_word(Punct p) {
    switch (p) {
    case Punct::OnTime:  return "ontime";
    case Punct::Late:    return "late";
    case Punct::Unknown: return "unknown";
    }
    return "?";
}

RoutineState routine_state(const NodeSource& src, const NodeId& id, std::int64_t now) {
    RoutineState s;
    const Node* n = src.find(id);
    if (!n || !n->task.is_task || !n->task.repeat.on()) return s;
    s.on = true;

    std::vector<Occurrence> all;
    for (const auto& r : src.history()) {
        if (r.id != id) continue;
        Occurrence o;
        o.when = r.when;
        o.due  = r.due;
        if (r.due != 0) {
            const int gap = days_between(r.due, r.when);
            o.punct     = gap <= 0 ? Punct::OnTime : Punct::Late;
            o.late_days = std::max(0, gap);
        }
        all.push_back(o);
    }
    std::stable_sort(all.begin(), all.end(),
                     [](const Occurrence& a, const Occurrence& b) { return a.when < b.when; });
    s.total = static_cast<int>(all.size());
    if (!all.empty()) s.last_done = all.back().when;
    const std::size_t from = all.size() > kRoutineWindow ? all.size() - kRoutineWindow : 0;
    s.recent.assign(all.begin() + static_cast<std::ptrdiff_t>(from), all.end());
    for (const auto& o : s.recent) {
        if (o.punct == Punct::OnTime) { ++s.on_time; ++s.known; }
        if (o.punct == Punct::Late)   { ++s.late;    ++s.known; }
    }

    s.due = effective_due(src, id);
    if (s.due != 0 && s.due < now && !n->task.done) {
        // Each whole interval past the due is an occurrence that did not happen.
        int k = 0;
        while (k < 1000 && repeat_add(s.due, n->task.repeat, k + 1) <= now) ++k;
        s.missed = k;
    }
    return s;
}

std::string routine_line(const RoutineState& s, std::int64_t /*now*/) {
    if (!s.on) return {};
    if (s.total == 0) return "Not done yet";
    std::string out;
    if (s.known > 0) {
        out = s.known == 1 ? (s.on_time ? std::string("On time the last time")
                                        : std::string("Late the last time"))
                           : std::to_string(s.on_time) + " of the last " + std::to_string(s.known) +
                                 " on time";
    } else {
        out = s.total == 1 ? std::string("Done once") : "Done " + std::to_string(s.total) + " times";
    }
    out += "  ·  last done " + short_day(s.last_done);
    return out;
}

std::string slipped_line(const RoutineState& s) {
    if (!s.slipped()) return {};
    return "Slipped — " + std::to_string(s.missed) + " missed since " + short_day(s.due);
}

std::string routine_short(const RoutineState& s) {
    if (s.slipped()) return "slipped";
    if (s.known == 0) return {};
    return std::to_string(s.on_time) + " of " + std::to_string(s.known);
}

std::vector<NodeId> routine_list(const NodeSource& src, std::int64_t now) {
    struct Row { NodeId id; RoutineState s; };
    std::vector<Row> rows;
    std::vector<NodeId> stack;
    const auto roots = src.children({});
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back(*it);
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        const auto kids = src.children(id);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
        const Node* n = src.find(id);
        if (!n || !n->task.is_task || !n->task.repeat.on()) continue;
        if (availability(src, id, now) == Avail::Dropped) continue;
        rows.push_back({id, routine_state(src, id, now)});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.s.missed != b.s.missed) return a.s.missed > b.s.missed;
        if ((a.s.due == 0) != (b.s.due == 0)) return a.s.due != 0;
        return a.s.due < b.s.due;
    });
    std::vector<NodeId> out;
    for (auto& r : rows) out.push_back(std::move(r.id));
    return out;
}

}  // namespace jot::core
