#include "core/Deadline.hpp"
#include "core/DoneWhen.hpp"
#include "core/Packet.hpp"
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

// Whole calendar days from a's day to b's day -- on dates, so DST is one day.
// (RowLook's rule; a copy rather than a new public seam for one function.)
int days_between(std::int64_t a, std::int64_t b) {
    std::tm x = local(a), y = local(b);
    x.tm_hour = y.tm_hour = 12;
    x.tm_min = y.tm_min = x.tm_sec = y.tm_sec = 0;
    x.tm_isdst = y.tm_isdst = -1;
    const double secs = std::difftime(std::mktime(&y), std::mktime(&x));
    return static_cast<int>(secs >= 0 ? (secs + 43200) / 86400 : (secs - 43200) / 86400);
}

// 00:00 of `when`'s day moved back `days` calendar days.
std::int64_t day_back(std::int64_t when, int days) {
    std::tm t = local(when);
    t.tm_mday -= days;
    t.tm_hour = t.tm_min = t.tm_sec = 0;
    t.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&t));
}

bool dealt_with(const Node& c) {
    return c.task.done || c.task.project == ProjectState::Completed ||
           c.task.project == ProjectState::Dropped;
}

std::string plural(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string quoted(const std::string& t) {
    return "“" + (t.empty() ? std::string("Untitled") : t) + "”";
}

}  // namespace

const char* pace_word(Pace p) {
    switch (p) {
    case Pace::NoDate:   return "nodate";
    case Pace::Finished: return "finished";
    case Pace::Ready:    return "ready";
    case Pace::OnTrack:  return "ontrack";
    case Pace::Short:    return "short";
    case Pace::Late:     return "late";
    }
    return "?";
}

DeadlineState deadline_state(const NodeSource& src, const NodeId& id, std::int64_t now) {
    DeadlineState s;
    const Node* n = src.find(id);
    if (!n) return s;
    const ProjectState ps = project_state(*n);
    if ((n->task.is_task && n->task.done) || ps == ProjectState::Completed ||
        ps == ProjectState::Dropped) {
        s.pace = Pace::Finished;
        return s;
    }

    // ── what is left ────────────────────────────────────────────────────────
    const auto self_available = [&] {
        return n->task.is_task && availability(src, id, now) == Avail::Available;
    };
    if (effective_done_when(*n) == DoneWhen::Items) {
        const PacketState p = packet_state(n->body);
        s.steps   = p.total - p.in;
        s.unsized = s.steps;
        // The items are lines, not nodes: the work itself is what you open.
        if (s.steps > 0 && self_available()) { s.next = id; s.next_title = n->title; }
    } else {
        bool any_todo = false;
        for (const auto& k : src.children(id)) {
            const Node* c = src.find(k);
            if (!c || !c->task.is_task) continue;
            any_todo = true;
            if (dealt_with(*c)) continue;
            ++s.steps;
            if (c->task.estimate > 0) s.minutes += c->task.estimate;
            else ++s.unsized;
            // The first step that can start NOW -- the sequence's next, or in
            // a parallel project the first one free.
            if (s.next.empty() && availability(src, k, now) == Avail::Available) {
                s.next = k;
                s.next_title = c->title;
            }
        }
        if (!any_todo && n->task.is_task) {
            s.steps = 1;   // it is its own step
            if (n->task.estimate > 0) s.minutes = n->task.estimate;
            else s.unsized = 1;
            if (self_available()) { s.next = id; s.next_title = n->title; }
        }
    }

    s.due = effective_due(src, id);
    if (s.due == 0) { s.pace = Pace::NoDate; return s; }
    s.days_left = days_between(now, s.due);
    if (s.steps == 0) {
        s.pace = Pace::Ready;
        if (self_available()) { s.next = id; s.next_title = n->title; }
        return s;
    }

    // ── the rule ────────────────────────────────────────────────────────────
    const int hours = (s.minutes + 59) / 60;
    s.need     = std::max(s.steps, hours) + 1;
    s.start_by = day_back(s.due, s.need);
    if (s.due < now)                  s.pace = Pace::Late;
    else if (s.days_left <= s.need)   s.pace = Pace::Short;
    else                              s.pace = Pace::OnTrack;
    return s;
}

std::string runway_text(const DeadlineState& s) {
    if (s.pace == Pace::NoDate || s.pace == Pace::Finished) return {};
    std::string out;
    auto add = [&out](const std::string& part) {
        if (part.empty()) return;
        if (!out.empty()) out += "  ·  ";
        out += part;
    };
    if (s.pace == Pace::Ready) add("nothing left");
    else add(plural(s.steps, "step", "steps"));
    if (s.minutes > 0) {
        std::string m = "~" + format_estimate(s.minutes);
        if (s.unsized > 0) m += " (" + std::to_string(s.unsized) + " not sized)";
        add(m);
    }
    if (s.pace == Pace::Late || s.days_left < 0)
        add(s.days_left < 0 ? plural(-s.days_left, "day late", "days late") : "late today");
    else if (s.days_left == 0) add("due today");
    else add(plural(s.days_left, "day out", "days out"));
    return out;
}

std::string pace_text(const DeadlineState& s, std::int64_t now) {
    const std::string on_today = s.next.empty() ? std::string("nothing can start yet")
                                                : quoted(s.next_title) + " is on Today";
    switch (s.pace) {
    case Pace::NoDate:   return "Give it a due date and jot works back from it.";
    case Pace::Finished: return "Done.";
    case Pace::Ready:    return "Everything is done — tick it.";
    case Pace::Short:    return "Running short — " + on_today;
    case Pace::Late:     return "Late — " + on_today;
    case Pace::OnTrack: {
        std::string out = "On track — ";
        const int gap = days_between(now, s.start_by);
        if (gap <= 0) out += "start today";
        else {
            const std::tm d = local(s.start_by);
            char buf[32];
            std::strftime(buf, sizeof buf, "%a ", &d);
            char mon[16];
            std::strftime(mon, sizeof mon, " %b", &d);
            out += "start by " + std::string(buf) + std::to_string(d.tm_mday) + mon;
        }
        // How much a day -- or a week, when it is more than two weeks out
        // (5 minutes a day reads as nothing; 15 minutes a week is a plan).
        // Rounded UP to 5 minutes: under-promising the work is the failure.
        if (s.minutes > 0 && s.days_left > 0) {
            const bool weekly = s.days_left > 14;
            const int  span   = weekly ? std::max(1, s.days_left / 7) : s.days_left;
            int per = (s.minutes + span - 1) / span;
            per = std::max(5, (per + 4) / 5 * 5);
            out += "  ·  about " + format_estimate(per) + (weekly ? " a week" : " a day");
        }
        return out;
    }
    }
    return {};
}

std::string deadline_mark(const DeadlineState& s) {
    switch (s.pace) {
    case Pace::OnTrack: return "⏳ " + std::to_string(s.days_left) + "d";
    case Pace::Short:   return "⏳ short";
    case Pace::Late:    return "⏳ late";
    case Pace::Ready:   return "⏳ ready";
    default:            return {};
    }
}

std::vector<Pull> deadline_pulls(const NodeSource& src, std::int64_t now) {
    std::vector<Pull> out;
    std::vector<NodeId> stack;
    const auto roots = src.children({});
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back(*it);
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        const Node* n = src.find(id);
        if (!n) continue;
        if (n->task.deadline) {
            const DeadlineState s = deadline_state(src, id, now);
            if (s.pulls()) out.push_back({id, s.next});
        }
        const auto kids = src.children(id);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    }
    return out;
}

}  // namespace jot::core
