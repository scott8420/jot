#include "core/RowLook.hpp"
#include "core/Deadline.hpp"
#include "core/Routine.hpp"
#include "core/DoneWhen.hpp"
#include "core/Repeat.hpp"
#include "core/Tasks.hpp"

#include <ctime>

#include <cctype>
#include <cstdio>

namespace jot::core {
namespace {

std::tm local(std::int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm lt{};
    localtime_r(&tt, &lt);
    return lt;
}

// Whole calendar days from a's day to b's day (b later = positive). Counted
// on dates, not seconds, so a DST night is still one day.
int day_gap(std::int64_t a, std::int64_t b) {
    std::tm x = local(a), y = local(b);
    x.tm_hour = y.tm_hour = 12;
    x.tm_min = y.tm_min = x.tm_sec = y.tm_sec = 0;
    x.tm_isdst = y.tm_isdst = -1;
    const double secs = std::difftime(std::mktime(&y), std::mktime(&x));
    return static_cast<int>(secs >= 0 ? (secs + 43200) / 86400 : (secs - 43200) / 86400);
}

bool bare_time(const std::tm& t) {
    return (t.tm_hour == 0 && t.tm_min == 0 && t.tm_sec == 0) ||
           (t.tm_hour == 23 && t.tm_min == 59 && t.tm_sec == 59);
}

std::string fmt(const std::tm& t, const char* f) {
    char buf[40];
    std::strftime(buf, sizeof buf, f, &t);
    return buf;
}

}  // namespace

const char* row_state_word(RowState s) {
    switch (s) {
    case RowState::Done:      return "done";
    case RowState::Overdue:   return "overdue";
    case RowState::DueToday:  return "today";
    case RowState::OnHold:    return "hold";
    case RowState::Waiting:   return "waiting";
    case RowState::Deferred:  return "deferred";
    case RowState::Flagged:   return "flagged";
    case RowState::Available: return "available";
    }
    return "available";
}

std::string short_due(std::int64_t due, std::int64_t now) {
    if (due == 0) return {};
    const std::tm d = local(due);
    const int gap = day_gap(now, due);
    const std::string clock = bare_time(d) ? std::string() : " " + fmt(d, "%H:%M");
    if (gap < 0) return std::to_string(-gap) + "d late";
    if (gap == 0) return (due < now ? "Overdue" : "Today") + clock;
    if (gap == 1) return "Tomorrow" + clock;
    if (gap < 7)  return fmt(d, "%a") + clock;
    const std::tm n = local(now);
    std::string day = fmt(d, "%b ") + std::to_string(d.tm_mday);
    if (d.tm_year != n.tm_year) day += " " + std::to_string(d.tm_year + 1900);
    return day;
}

RowLook row_look(const NodeSource& src, const Node& n, std::int64_t now) {
    RowLook r;
    const std::int64_t due = effective_due(src, n.id);
    r.due           = short_due(due, now);
    r.due_inherited = due != 0 && due != n.task.due;
    r.flagged       = effective_flagged(src, n.id);
    if (n.task.estimate > 0) r.estimate = format_estimate(n.task.estimate);
    if (n.task.repeat.on()) {
        r.repeat  = repeat_text(n.task.repeat);
        r.routine = routine_short(routine_state(src, n.id, now));   // s057
    }
    if (!n.task.done) {                                   // s054
        const DoneState ds = done_state(src, n.id);
        r.done_when = done_count(ds);
        r.done_met  = ds.met();
    }
    if (n.task.deadline) {                                // s055
        const DeadlineState dl = deadline_state(src, n.id, now);
        r.runway = runway_text(dl);
        if (!r.runway.empty()) r.pace = pace_word(dl.pace);
    }
    if (!n.task.feeds.empty())                            // s058
        if (const Node* g = src.find(n.task.feeds)) r.feeds = g->title.empty() ? std::string("Untitled") : g->title;
    if (!n.parent_id.empty())
        if (const Node* p = src.find(n.parent_id))
            r.project = p->title.empty() ? std::string("Untitled") : p->title;

    const Avail a = availability(src, n.id, now);
    if (n.task.done || a == Avail::Done)             r.state = RowState::Done;
    // Dropped is not "late": nobody is going to do it. Grey, before Overdue.
    else if (a == Avail::Dropped)                    r.state = RowState::OnHold;
    else if (due != 0 && due < now)                  r.state = RowState::Overdue;
    else if (due != 0 && day_gap(now, due) == 0)     r.state = RowState::DueToday;
    else if (a == Avail::OnHold)                     r.state = RowState::OnHold;
    else if (a == Avail::Blocked)                    r.state = RowState::Waiting;
    else if (a == Avail::Deferred)                   r.state = RowState::Deferred;
    else if (r.flagged)                              r.state = RowState::Flagged;
    else                                             r.state = RowState::Available;
    return r;
}

ViewSummary summarize(const NodeSource& src, const std::vector<NodeId>& ids, std::int64_t now) {
    ViewSummary v;
    for (const NodeId& id : ids) {
        const Node* n = src.find(id);
        if (!n || !n->task.is_task || n->task.done) continue;
        ++v.todo;
        const RowState st = row_look(src, *n, now).state;
        if (st == RowState::Overdue)  ++v.late;
        if (st == RowState::DueToday) ++v.today;
        if (effective_flagged(src, id)) ++v.flagged;
        if (n->task.estimate > 0) v.minutes += n->task.estimate;
        else ++v.unsized;
    }
    return v;
}

std::string summary_text(const ViewSummary& v) {
    if (v.todo == 0) return "Nothing to do";
    std::string s = std::to_string(v.todo) + " to do";
    auto part = [&](const std::string& p) { s += "  \u00b7  " + p; };
    if (v.late)  part(std::to_string(v.late) + " late");
    if (v.today) part(std::to_string(v.today) + " due today");
    if (v.minutes > 0) {
        std::string t = "~" + format_estimate(v.minutes);
        if (v.unsized) t += " + " + std::to_string(v.unsized) + " not sized";
        part(t);
    }
    return s;
}

StepCount step_count(const NodeSource& src, const NodeId& id) {
    StepCount c;
    for (const auto& k : src.children(id))
        if (const Node* n = src.find(k); n && n->task.is_task) {
            ++c.total;
            if (n->task.done) ++c.done;
        }
    return c;
}

std::string compact_due(const std::string& chip) {
    if (chip.rfind("Today", 0) == 0 || chip.rfind("Overdue", 0) == 0) return chip;
    // A trailing " H:MM" / " HH:MM".
    const auto sp = chip.rfind(' ');
    if (sp == std::string::npos) return chip;
    const std::string t = chip.substr(sp + 1);
    const auto colon = t.find(':');
    if (colon == std::string::npos || colon == 0 || colon > 2 || t.size() != colon + 3) return chip;
    for (std::size_t k = 0; k < t.size(); ++k)
        if (k != colon && (t[k] < '0' || t[k] > '9')) return chip;
    return chip.substr(0, sp);
}

std::string accent_css(double r, double g, double b) {
    for (double v : {r, g, b})
        if (!(v >= 0.0 && v <= 1.0)) return {};   // also catches NaN
    auto byte = [](double v) { return static_cast<int>(v * 255.0 + 0.5); };
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", byte(r), byte(g), byte(b));
    return buf;
}

bool is_hex_colour(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return false;
    for (std::size_t i = 1; i < 7; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

const std::vector<AccentPreset>& accent_presets() {
    static const std::vector<AccentPreset> k{
        {"Blue", "#3584e4"},  {"Teal", "#2190a4"}, {"Green", "#3a944a"},
        {"Yellow", "#c88800"}, {"Orange", "#ed5b00"}, {"Red", "#e62d42"},
        {"Pink", "#d56199"},  {"Purple", "#9141ac"}, {"Slate", "#6f8396"},
    };
    return k;
}

std::string accent_name(const std::string& hex) {
    if (hex.empty()) return {};
    std::string low = hex;
    for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& p : accent_presets())
        if (low == p.hex) return p.name;
    return "Custom";
}

}  // namespace jot::core
