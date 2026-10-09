#include "core/Feeders.hpp"
#include "core/Repeat.hpp"
#include "core/Tasks.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <functional>

namespace jot::core {
namespace {

int days_between(std::int64_t a, std::int64_t b) {
    auto local = [](std::int64_t t) {
        std::time_t tt = static_cast<std::time_t>(t);
        std::tm lt{};
        localtime_r(&tt, &lt);
        lt.tm_hour = 12;
        lt.tm_min = lt.tm_sec = 0;
        lt.tm_isdst = -1;
        return lt;
    };
    std::tm x = local(a), y = local(b);
    const double secs = std::difftime(std::mktime(&y), std::mktime(&x));
    return static_cast<int>(secs >= 0 ? (secs + 43200) / 86400 : (secs - 43200) / 86400);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}
std::string shown(const Node& n) { return n.title.empty() ? std::string("Untitled") : n.title; }

std::string short_day(std::int64_t when) {
    std::time_t tt = static_cast<std::time_t>(when);
    std::tm d{};
    localtime_r(&tt, &d);
    char wd[16], mon[16];
    std::strftime(wd, sizeof wd, "%a", &d);
    std::strftime(mon, sizeof mon, "%b", &d);
    return std::string(wd) + " " + std::to_string(d.tm_mday) + " " + mon;
}

void walk(const NodeSource& src, const std::function<void(const Node&, const std::string&)>& fn) {
    struct Frame { NodeId id; std::string path; };
    std::vector<Frame> stack;
    const auto roots = src.children({});
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back({*it, ""});
    while (!stack.empty()) {
        Frame f = std::move(stack.back());
        stack.pop_back();
        const Node* n = src.find(f.id);
        if (!n) continue;
        fn(*n, f.path);
        const std::string below = f.path.empty() ? shown(*n) : f.path + " › " + shown(*n);
        const auto kids = src.children(f.id);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back({*it, below});
    }
}

}  // namespace

std::vector<NodeId> feeders_of(const NodeSource& src, const NodeId& goal) {
    std::vector<NodeId> out;
    if (goal.empty()) return out;
    walk(src, [&](const Node& n, const std::string&) {
        if (n.task.feeds == goal && n.id != goal && n.task.project != ProjectState::Dropped)
            out.push_back(n.id);
    });
    return out;
}

FeedState feed_state(const NodeSource& src, const NodeId& goal, std::int64_t now) {
    FeedState f;
    f.due = effective_due(src, goal);
    if (f.due != 0 && f.due > now) f.weeks = days_between(now, f.due) / 7;
    for (const auto& id : feeders_of(src, goal)) {
        const Node* n = src.find(id);
        if (!n) continue;
        FeederState s;
        s.id      = id;
        s.title   = n->title;
        s.routine = routine_state(src, id, now);
        if (s.routine.on) {
            const std::int64_t d0 = s.routine.due;
            if (d0 != 0) {
                // The current occurrence (late or not), then each one after
                // now, up to the goal's due. Missed ones in between are gone.
                if (f.due == 0 || d0 <= f.due) s.to_come = 1;
                if (f.due != 0)
                    for (int k = 1; k < 1000; ++k) {
                        const std::int64_t dk = repeat_add(d0, n->task.repeat, k);
                        if (dk > f.due) break;
                        if (dk > now) ++s.to_come;
                    }
            } else {
                s.to_come = 1;   // undated: at least the next one
            }
            if (s.routine.slipped()) ++f.slipped;
        } else {
            s.done    = n->task.done;
            s.to_come = s.done ? 0 : 1;
        }
        s.minutes = s.to_come * n->task.estimate;
        f.minutes += s.minutes;
        if (s.to_come > 0 && n->task.estimate == 0) ++f.unsized;
        f.feeders.push_back(std::move(s));
    }
    return f;
}

std::string feed_line(const FeedState& f) {
    if (f.feeders.empty()) return {};
    const int n = static_cast<int>(f.feeders.size());
    std::string out = std::to_string(n) + (n == 1 ? " feeder" : " feeders");
    if (f.minutes > 0) {
        out += "  ·  ~" + format_estimate(f.minutes) + (f.due != 0 ? " before the due" : " next");
        if (f.weeks >= 1) {
            int per = (f.minutes + f.weeks - 1) / f.weeks;
            per = std::max(5, (per + 4) / 5 * 5);
            out += "  ·  ~" + format_estimate(per) + " a week";
        }
    }
    if (f.unsized > 0) out += "  ·  " + std::to_string(f.unsized) + " not sized";
    if (f.slipped > 0) out += "  ·  " + std::to_string(f.slipped) + " slipped";
    return out;
}

std::string feeder_line(const FeederState& s) {
    if (!s.routine.on) return std::string("once  ·  ") + (s.done ? "done" : "to do");
    std::string out;
    if (s.routine.slipped())
        out = "slipped, " + std::to_string(s.routine.missed) + " missed";
    else if (s.routine.last_done != 0)
        out = "last done " + short_day(s.routine.last_done);
    else
        out = "not done yet";
    return out;
}

std::vector<FeedTarget> feed_targets(const NodeSource& src, const NodeId& feeder,
                                     const std::string& query) {
    std::vector<FeedTarget> all;
    walk(src, [&](const Node& n, const std::string& path) {
        if (n.id == feeder) return;
        all.push_back({n.id, shown(n), path, n.task.deadline});
    });
    const std::string q = lower(trim(query));
    if (!q.empty()) {
        std::vector<FeedTarget> starts, contains, inpath;
        for (auto& t : all) {
            const std::string tl = lower(t.title);
            if (tl.rfind(q, 0) == 0)                       starts.push_back(std::move(t));
            else if (tl.find(q) != std::string::npos)      contains.push_back(std::move(t));
            else if (lower(t.path).find(q) != std::string::npos) inpath.push_back(std::move(t));
        }
        all.clear();
        for (auto* v : {&starts, &contains, &inpath})
            for (auto& t : *v) all.push_back(std::move(t));
    }
    std::stable_partition(all.begin(), all.end(), [](const FeedTarget& t) { return t.deadline; });
    return all;
}

// ── s072: a slipped feeder speaks ───────────────────────────────────────────
std::string slip_key(const NodeId& id, std::int64_t due) {
    return std::string(kSlipPrefix) + id + "@" + std::to_string(due);
}

bool is_slip_key(const std::string& key) { return key.rfind(kSlipPrefix, 0) == 0; }

AnnounceResult feeder_slips(const NodeSource& src, std::int64_t now,
                            const std::vector<std::string>& announced,
                            const std::vector<Snoozed>& parked) {
    AnnounceResult r;
    std::tm tm{};
    const std::time_t t = static_cast<std::time_t>(now);
    localtime_r(&t, &tm);
    const bool hour_ok = tm.tm_hour >= 9;
    std::function<void(const NodeId&)> walk = [&](const NodeId& parent) {
        for (const auto& id : src.children(parent)) {
            const Node* n = src.find(id);
            if (!n) continue;
            walk(id);
            if (!n->task.is_task || n->task.done || n->task.feeds.empty()) continue;
            const Node* goal = src.find(n->task.feeds);
            if (!goal || (goal->task.is_task && goal->task.done)) continue;
            const RoutineState rs = routine_state(src, id, now);
            if (!rs.on || !rs.slipped()) continue;
            const std::string key = slip_key(id, rs.due);
            r.live.push_back(key);
            if (std::find(announced.begin(), announced.end(), key) != announced.end()) {
                r.keep.push_back(key);
                continue;
            }
            const bool held = std::any_of(parked.begin(), parked.end(), [&](const Snoozed& z) {
                return z.key == key && z.until > now;
            });
            if (!hour_ok || held) continue;
            Announcement a;
            a.id = id;
            a.key = key;
            a.summary = "\u201c" + (n->title.empty() ? std::string("Untitled") : n->title) + "\u201d has slipped";
            a.detail = "It feeds " + (goal->title.empty() ? std::string("Untitled") : goal->title) +
                       " \u00b7 " + slipped_line(rs);
            a.due = rs.due;
            r.to_show.push_back(std::move(a));
        }
    };
    walk("");
    return r;
}

}  // namespace jot::core
