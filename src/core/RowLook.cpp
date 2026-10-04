#include "core/RowLook.hpp"
#include "core/Repeat.hpp"
#include "core/Tasks.hpp"

#include <ctime>

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
    if (n.task.repeat.on())  r.repeat   = repeat_text(n.task.repeat);
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

}  // namespace jot::core
