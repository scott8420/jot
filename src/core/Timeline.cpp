#include "core/Timeline.hpp"
#include "core/Errands.hpp"
#include "core/Feeders.hpp"
#include "core/Repeat.hpp"
#include "core/Review.hpp"
#include "core/Routine.hpp"
#include "core/Search.hpp"
#include "core/Tags.hpp"
#include "core/Markdown.hpp"
#include "core/Tasks.hpp"

#include <algorithm>
#include <set>
#include <ctime>
#include <cmath>
#include <functional>
#include <map>

namespace jot::core {

namespace {

constexpr int kBefore = 14;   // the span shows at least two weeks back ...
constexpr int kAfter  = 56;   // ... and eight weeks on

std::string title_of(const Node& n) { return n.title.empty() ? std::string("Untitled") : n.title; }

// The same reading as RowLook's state, for a todo -- without the routine and
// packet lines RowLook also works out, which a timeline of hundreds of dots
// does not need.
TlTone todo_tone(const NodeSource& src, const Node& n, std::int64_t now) {
    const Avail a = availability(src, n.id, now);
    const std::int64_t due = effective_due(src, n.id);
    if (n.task.done || a == Avail::Done) return TlTone::Done;
    if (due != 0 && due < now) return TlTone::Late;
    if (due != 0 && day_start(due) == day_start(now)) return TlTone::Today;
    if (a == Avail::OnHold || a == Avail::Blocked || a == Avail::Deferred) return TlTone::Waiting;
    if (effective_flagged(src, n.id)) return TlTone::Flagged;
    return TlTone::Available;
}

TlTone project_tone(const NodeSource& src, const Node& n, std::int64_t now) {
    const ProjectState st = effective_state(src, n.id);
    if (st == ProjectState::Completed || n.task.done) return TlTone::Done;
    if (st == ProjectState::OnHold) return TlTone::Waiting;
    const std::int64_t due = n.task.due;
    if (due != 0 && due < now) return TlTone::Late;
    if (due != 0 && day_start(due) == day_start(now)) return TlTone::Today;
    if (n.task.defer != 0 && n.task.defer > now) return TlTone::Waiting;
    if (n.task.flagged) return TlTone::Flagged;
    return TlTone::Available;
}

bool dropped(const NodeSource& src, const Node& n, std::int64_t now) {
    if (n.task.is_task && availability(src, n.id, now) == Avail::Dropped) return true;
    return effective_state(src, n.id) == ProjectState::Dropped;
}

int kind_rank(TlKind k) { return k == TlKind::Project ? 0 : k == TlKind::Todo ? 1 : 2; }

}  // namespace

int day_index(std::int64_t first, std::int64_t day) {
    return static_cast<int>(std::lround(static_cast<double>(day - first) / 86400.0));
}

std::int64_t day_at(std::int64_t first, int n) {
    // Noon of the target day, then back to its 00:00 -- a DST night is 23 or
    // 25 hours, and noon is never more than an hour off either way.
    return day_start(first + static_cast<std::int64_t>(n) * 86400 + 12 * 3600);
}

Timeline build_timeline(const NodeSource& src, const TlShow& show, std::int64_t now) {
    Timeline t;
    std::map<std::int64_t, std::vector<TlItem>> by_day;
    std::vector<TlItem> someday;

    // A walk in document order. `carrier` is the item undated work under it
    // rides in (its steps); "" when nothing above is on the line.
    // Steps are gathered into a side table keyed by the carrier's id, because
    // a vector of a day can reallocate while the walk is still adding to it.
    std::map<NodeId, std::vector<NodeId>> steps_of;

    std::function<void(const NodeId&, const NodeId&)> visit =
        [&](const NodeId& id, const NodeId& carrier) {
        const Node* n = src.find(id);
        if (!n) return;
        if (dropped(src, *n, now)) return;   // nobody is going to do it, nor what is under it
        NodeId next_carrier = carrier;

        const bool project = is_project(src, id);
        const bool todo    = !project && n->task.is_task;
        TlItem it;
        it.id = id;
        it.title = title_of(*n);
        it.estimate = n->task.estimate;
        bool place = false;

        if (project) {
            it.kind = TlKind::Project;
            it.tone = project_tone(src, *n, now);
            if (it.tone == TlTone::Done) {
                if (n->task.finished) { it.why = TlWhy::Done; it.day = day_start(n->task.finished); place = true; }
            } else if (n->task.due) {
                it.why = TlWhy::Due; it.day = day_start(n->task.due); place = true;
            } else if (n->task.defer) {
                it.why = TlWhy::Starts; it.day = day_start(n->task.defer); place = true;
            } else {
                it.why = TlWhy::Someday;
            }
            if (show.projects && (place || show.someday)) {
                if (place) by_day[it.day].push_back(it); else someday.push_back(it);
                next_carrier = id;
            }
        } else if (todo) {
            it.kind = TlKind::Todo;
            it.tone = todo_tone(src, *n, now);
            if (it.tone == TlTone::Done) {
                if (n->task.finished) { it.why = TlWhy::Done; it.day = day_start(n->task.finished); place = true; }
                else return;   // done, and nobody knows when: not on a line
            } else if (n->task.due) {
                it.why = TlWhy::Due; it.day = day_start(n->task.due); place = true;
            } else if (!carrier.empty()) {
                steps_of[carrier].push_back(id);   // rides in what is above it
                // It may carry undated work of its own: that rides with the same carrier.
                for (const auto& c : src.children(id)) visit(c, carrier);
                return;
            } else if (n->task.defer) {
                it.why = TlWhy::Starts; it.day = day_start(n->task.defer); place = true;
            } else {
                it.why = TlWhy::Someday;
            }
            if (show.todos && (place || show.someday)) {
                if (place) by_day[it.day].push_back(it); else someday.push_back(it);
                next_carrier = id;
            }
        } else {
            it.kind = TlKind::Note;
            it.tone = TlTone::Note;
            it.why  = TlWhy::Made;
            if (show.notes && n->created) {
                it.day = day_start(n->created);
                by_day[it.day].push_back(it);
            }
        }

        for (const auto& c : src.children(id)) visit(c, next_carrier);
    };
    for (const auto& r : src.children("")) visit(r, "");

    auto fill_steps = [&](TlItem& it) {
        if (auto f = steps_of.find(it.id); f != steps_of.end()) it.steps = f->second;
    };

    const std::int64_t today = day_start(now);
    t.first = day_at(today, -kBefore);
    t.last  = day_at(today, kAfter);
    for (auto& [day, items] : by_day) {
        std::stable_sort(items.begin(), items.end(), [](const TlItem& a, const TlItem& b) {
            if (kind_rank(a.kind) != kind_rank(b.kind)) return kind_rank(a.kind) < kind_rank(b.kind);
            return (a.tone == TlTone::Late) > (b.tone == TlTone::Late);
        });
        for (auto& it : items) {
            fill_steps(it);
            if (it.tone == TlTone::Late) ++t.late;
        }
        t.dated += items.size();
        t.first = std::min(t.first, day);
        t.last  = std::max(t.last, day);
        t.days.push_back({day, std::move(items)});
    }
    for (auto& it : someday) fill_steps(it);
    t.someday = std::move(someday);

    // ── threads: a goal and its feeders' days ahead (s058) ─────────────────
    if (show.todos || show.projects) {
        std::function<void(const NodeId&)> goals = [&](const NodeId& id) {
            const Node* g = src.find(id);
            if (!g) return;
            const std::int64_t gdue = effective_due(src, id);
            if (gdue > now && !g->task.done) {
                const auto feeders = feeders_of(src, id);
                if (!feeders.empty()) {
                    TlThread th;
                    th.goal = id;
                    th.title = title_of(*g);
                    th.due_day = day_start(gdue);
                    for (const auto& fid : feeders) {
                        const Node* f = src.find(fid);
                        if (!f || f->task.done) continue;
                        const RoutineState rs = routine_state(src, fid, now);
                        const bool slipped = rs.slipped();
                        th.slipped = th.slipped || slipped;
                        std::int64_t d = effective_due(src, fid);
                        if (d == 0) continue;
                        if (!f->task.repeat.on()) {
                            if (d <= gdue) th.beads.push_back({fid, day_start(d), slipped});
                            continue;
                        }
                        // The current occurrence (late or not), then each one after it.
                        int guard = 0;
                        for (std::int64_t at = d; at <= gdue && guard < 400; ++guard) {
                            th.beads.push_back({fid, day_start(at), slipped && at == d});
                            const std::int64_t nx = repeat_add(at, f->task.repeat, 1);
                            if (nx <= at) break;
                            at = nx;
                        }
                    }
                    std::stable_sort(th.beads.begin(), th.beads.end(),
                                     [](const TlBead& a, const TlBead& b) { return a.day < b.day; });
                    for (const auto& b : th.beads) t.first = std::min(t.first, b.day);
                    t.last = std::max(t.last, th.due_day);
                    t.threads.push_back(std::move(th));
                }
            }
            for (const auto& c : src.children(id)) goals(c);
        };
        for (const auto& r : src.children("")) goals(r);
    }
    if (show.links) t.links = timeline_links(src, t);   // s064
    return t;
}

const char* tl_group_word(TlGroup g) {
    switch (g) {
    case TlGroup::Day:     return "day";
    case TlGroup::Place:   return "place";
    case TlGroup::Purpose: return "purpose";
    }
    return "?";
}

namespace {

// What a node's work is FOR: the goal it feeds; else, walking up from it,
// the first that is a project or a goal (has feeders). "" = loose.
NodeId purpose_of(const NodeSource& src, const NodeId& id) {
    const Node* n = src.find(id);
    if (!n) return {};
    if (!n->task.feeds.empty() && src.find(n->task.feeds)) return n->task.feeds;
    for (NodeId at = id; !at.empty();) {
        const Node* x = src.find(at);
        if (!x) break;
        if (is_project(src, at) || !feeders_of(src, at).empty()) return at;
        at = x->parent_id;
    }
    return {};
}

std::string lane_line(const TlLane& l) {
    int things = 0, minutes = 0, late = 0;
    auto add = [&](const TlItem& it) {
        ++things;
        if (it.tone != TlTone::Done) minutes += it.estimate;
        if (it.tone == TlTone::Late) ++late;
    };
    for (const auto& d : l.days) for (const auto& it : d.items) add(it);
    for (const auto& it : l.someday) add(it);
    std::string s = std::to_string(things) + (things == 1 ? " thing" : " things");
    if (minutes) s += " \u00b7 ~" + format_estimate(minutes);
    if (late) s += " \u00b7 " + std::to_string(late) + " late";
    return s;
}

}  // namespace

std::vector<TlLane> timeline_lanes(const NodeSource& src, const Timeline& t, TlGroup g,
                                   std::int64_t now) {
    std::vector<TlLane> out;
    if (g == TlGroup::Day) {
        TlLane l;
        l.days = t.days;
        l.someday = t.someday;
        l.line = lane_line(l);
        out.push_back(std::move(l));
        return out;
    }
    // The names as written, for places (#at/Town reads "Town").
    std::map<std::string, std::string> spelled;
    if (g == TlGroup::Place)
        for (const auto& tg : tag_list(src, now))
            if (is_place_key(tg.key)) spelled.emplace(tg.key, tg.name);

    auto keys_of = [&](const TlItem& it) {
        std::vector<std::string> ks;
        if (g == TlGroup::Place) {
            if (const Node* n = src.find(it.id)) ks = node_places(*n);
        } else {
            const NodeId p = purpose_of(src, it.id);
            if (!p.empty()) ks.push_back(p);
        }
        if (ks.empty()) ks.push_back("");
        return ks;
    };
    std::map<std::string, std::size_t> at;   // lane key -> index in out
    auto lane = [&](const std::string& key) -> TlLane& {
        if (auto f = at.find(key); f != at.end()) return out[f->second];
        TlLane l;
        l.key = key;
        if (key.empty()) l.title = g == TlGroup::Place ? "No place" : "Loose ends";
        else if (g == TlGroup::Place) {
            const auto sp = spelled.find(key);
            l.title = place_title(sp != spelled.end() ? sp->second : key);
        } else if (const Node* n = src.find(key)) {
            l.title = n->title.empty() ? std::string("Untitled") : n->title;
        }
        at[key] = out.size();
        out.push_back(std::move(l));
        return out.back();
    };
    for (const auto& d : t.days)
        for (const auto& it : d.items)
            for (const auto& k : keys_of(it)) {
                TlLane& l = lane(k);
                if (l.days.empty() || l.days.back().day != d.day) l.days.push_back({d.day, {}});
                l.days.back().items.push_back(it);
            }
    for (const auto& it : t.someday)
        for (const auto& k : keys_of(it)) lane(k).someday.push_back(it);

    // Soonest open dated thing first (late counts as soonest); the
    // catch-all last; lanes with nothing open after the rest, by name.
    auto rank = [&](const TlLane& l) -> std::int64_t {
        for (const auto& d : l.days)
            for (const auto& it : d.items)
                if (it.tone != TlTone::Done && it.kind != TlKind::Note) return d.day;
        return INT64_MAX;
    };
    std::stable_sort(out.begin(), out.end(), [&](const TlLane& a, const TlLane& b) {
        if (a.key.empty() != b.key.empty()) return b.key.empty();
        const auto ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        return a.title < b.title;
    });
    for (auto& l : out) l.line = lane_line(l);
    return out;
}

std::string timeline_summary(const Timeline& t, std::int64_t now) {
    const std::int64_t today = day_start(now);
    const std::int64_t week  = day_at(today, 7);
    int soon = 0;
    for (const auto& d : t.days)
        if (d.day >= today && d.day < week)
            for (const auto& it : d.items)
                if (it.kind != TlKind::Note && it.tone != TlTone::Done) ++soon;
    std::string s = std::to_string(t.dated) + " on the line";
    if (t.late) s += " · " + std::to_string(t.late) + " late";
    s += " · " + (soon ? std::to_string(soon) : std::string("nothing")) + " in the next 7 days";
    if (!t.someday.empty()) s += " · " + std::to_string(t.someday.size()) + " someday";
    return s;
}

std::vector<NodeId> timeline_matches(const NodeSource& src, const Timeline& t,
                                     const std::string& query, std::int64_t now) {
    std::vector<NodeId> out;
    if (!query_active(query)) return out;
    const Query q = parse_query(query);
    std::unordered_set<NodeId> seen;
    auto test = [&](const NodeId& id) {
        if (seen.count(id)) return;
        if (const Node* n = src.find(id); n && query_matches(src, *n, q, now)) {
            out.push_back(id);
            seen.insert(id);
        }
    };
    auto item = [&](const TlItem& it) {
        test(it.id);
        for (const auto& s : it.steps) test(s);
    };
    for (const auto& d : t.days)
        for (const auto& it : d.items) item(it);
    for (const auto& it : t.someday) item(it);
    return out;
}

std::vector<double> stack_down(const std::vector<TlSpan>& boxes, double hgap, double vgap,
                               const std::vector<double>& pinned) {
    std::vector<double> top(boxes.size(), 0.0);
    std::vector<bool> placed(boxes.size(), false);
    for (std::size_t i = 0; i < boxes.size() && i < pinned.size(); ++i)
        if (pinned[i] >= 0) { top[i] = pinned[i]; placed[i] = true; }
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        if (placed[i]) continue;
        const TlSpan& b = boxes[i];
        // The boxes already down that this one would touch side to side.
        std::vector<std::size_t> near;
        for (std::size_t j = 0; j < boxes.size(); ++j)
            if (placed[j] && boxes[j].x0 < b.x1 + hgap && b.x0 < boxes[j].x1 + hgap) near.push_back(j);
        // Candidates: the very top, and just under each neighbour. The lowest
        // candidate that clears every neighbour wins -- first fit.
        std::vector<double> cand{0.0};
        for (auto j : near) cand.push_back(top[j] + boxes[j].h + vgap);
        std::sort(cand.begin(), cand.end());
        for (double y : cand) {
            bool clear = true;
            for (auto j : near)
                if (y < top[j] + boxes[j].h + vgap && top[j] < y + b.h + vgap) { clear = false; break; }
            if (clear) { top[i] = y; break; }
        }
        placed[i] = true;
    }
    return top;
}

TlRect thumb_box(double cw, double ch, double tw, double th, const TlRect& v) {
    TlRect r;
    if (cw <= 0 || ch <= 0) return r;
    const double sx = tw / cw, sy = th / ch;
    r.x = std::clamp(v.x * sx, 0.0, tw);
    r.y = std::clamp(v.y * sy, 0.0, th);
    r.w = std::min(v.w * sx, tw - r.x);
    r.h = std::min(v.h * sy, th - r.y);
    return r;
}

void thumb_to_view(double cw, double ch, double tw, double th, double px, double py,
                   double vw, double vh, double& vx, double& vy) {
    const double cx = tw > 0 ? px / tw * cw : 0;
    const double cy = th > 0 ? py / th * ch : 0;
    vx = std::clamp(cx - vw / 2, 0.0, std::max(0.0, cw - vw));
    vy = std::clamp(cy - vh / 2, 0.0, std::max(0.0, ch - vh));
}

TlTone tl_tone(const NodeSource& src, const NodeId& id, std::int64_t now) {
    const Node* n = src.find(id);
    if (!n) return TlTone::Note;
    if (is_project(src, id)) return project_tone(src, *n, now);
    if (n->task.is_task) return todo_tone(src, *n, now);
    return TlTone::Note;
}

const char* tl_tone_word(TlTone t) {
    switch (t) {
    case TlTone::Late:      return "late";
    case TlTone::Today:     return "today";
    case TlTone::Flagged:   return "flagged";
    case TlTone::Available: return "available";
    case TlTone::Waiting:   return "waiting";
    case TlTone::Done:      return "done";
    case TlTone::Note:      return "note";
    }
    return "?";
}

}  // namespace jot::core

// ── s063: drag to another day ───────────────────────────────────────────────
namespace jot::core {

namespace {
// `day`'s date at `like`'s clock time; DST-safe (mktime decides the offset).
std::int64_t same_clock(std::int64_t day, std::int64_t like) {
    std::time_t d = static_cast<std::time_t>(day), l = static_cast<std::time_t>(like);
    std::tm td{}, tl{};
    localtime_r(&d, &td);
    localtime_r(&l, &tl);
    td.tm_hour = tl.tm_hour;
    td.tm_min  = tl.tm_min;
    td.tm_sec  = tl.tm_sec;
    td.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&td));
}
}  // namespace

TlMove timeline_move(const NodeSource& src, const NodeId& id, TlWhy why, bool riding,
                     std::int64_t day) {
    TlMove m;
    const Node* n = src.find(id);
    if (!n || n->protect || day <= 0) return m;
    std::int64_t old = 0;
    if (riding || why == TlWhy::Someday) {
        m.when = day_end(day);
    } else if (why == TlWhy::Due) {
        old = n->task.due;
        m.when = old ? same_clock(day, old) : day_end(day);
    } else if (why == TlWhy::Starts) {
        m.defer = true;
        old = n->task.defer;
        m.when = old ? same_clock(day, old) : day_start(day);
    } else {
        return m;   // Done, Made: history, not plans
    }
    if (old && day_start(old) == day_start(m.when)) return m;   // the day it already has
    m.ok = true;
    return m;
}

bool apply_timeline_move(NodeSource& src, const NodeId& id, const TlMove& m) {
    if (!m.ok) return false;
    return m.defer ? src.set_defer(id, m.when) : src.set_due(id, m.when);
}

}  // namespace jot::core

// ── s064: lines = links ─────────────────────────────────────────────────────
namespace jot::core {

std::vector<TlLink> timeline_links(const NodeSource& src, const Timeline& t) {
    std::vector<NodeId> on;
    std::unordered_set<NodeId> seen;
    auto take = [&](const TlItem& it) {
        if (seen.insert(it.id).second) on.push_back(it.id);
        for (const auto& s : it.steps) if (seen.insert(s).second) on.push_back(s);
    };
    for (const auto& d : t.days) for (const auto& it : d.items) take(it);
    for (const auto& it : t.someday) take(it);

    std::vector<TlLink> out;
    std::set<std::pair<NodeId, NodeId>> pairs;
    for (const auto& id : on) {
        const Node* n = src.find(id);
        if (!n || n->body.find(kJotScheme) == std::string::npos) continue;
        for (const auto& l : scan(n->body).links) {
            const NodeId to = link_node_id(l.target);
            if (to.empty() || to == id || !seen.count(to)) continue;
            auto key = id < to ? std::make_pair(id, to) : std::make_pair(to, id);
            if (pairs.insert(key).second) out.push_back({key.first, key.second});
        }
    }
    return out;
}

}  // namespace jot::core
