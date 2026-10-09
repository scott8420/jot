#include "core/Glance.hpp"

#include "core/Deadline.hpp"
#include "core/Errands.hpp"
#include "core/Forecast.hpp"
#include "core/Prefs.hpp"
#include "core/Project.hpp"
#include "core/Recents.hpp"   // s071: glance_of_folder
#include "core/Tasks.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <set>

// core/Glance.cpp -- see the header.

namespace jot::core {

namespace {

std::tm local_of(std::int64_t when) {
    const std::time_t t = static_cast<std::time_t>(when);
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm;
}

std::string strf(const char* fmt, std::int64_t when) {
    const std::tm tm = local_of(when);
    char buf[64];
    std::strftime(buf, sizeof buf, fmt, &tm);
    std::string out = buf;
    if (auto p = out.find("  "); p != std::string::npos) out.erase(p, 1);   // %e pads " 7"
    return out;
}

// "Fri 9 Oct" -- the short day, as the timeline and the Forecast say it.
std::string short_day(std::int64_t when) { return strf("%a %e %b", when); }

// Calendar days from a to b (both any instant in their days), DST-safe.
int days_between(std::int64_t a, std::int64_t b) {
    std::tm ta = local_of(day_start(a)), tb = local_of(day_start(b));
    ta.tm_hour = tb.tm_hour = 12;   // noon: a DST hour cannot move the day
    ta.tm_isdst = tb.tm_isdst = -1;
    const double s = std::difftime(std::mktime(&tb), std::mktime(&ta));
    return static_cast<int>(s >= 0 ? (s + 43200) / 86400 : (s - 43200) / 86400);
}

bool open_work(const NodeSource& src, const NodeId& id, std::int64_t now) {
    const Avail a = availability(src, id, now);
    return a != Avail::Done && a != Avail::Dropped && a != Avail::NotTask;
}

void join(std::string& out, const std::string& part) {
    if (part.empty()) return;
    if (!out.empty()) out += " · ";
    out += part;
}

// The quiet tail every line shares: where it sits, how long, where to go.
std::string tail(const NodeSource& src, const Node& n, bool with_parent, bool with_places) {
    std::string out;
    if (with_parent && !n.parent_id.empty())
        if (const Node* p = src.find(n.parent_id); p && !p->title.empty()) join(out, p->title);
    if (n.task.estimate > 0) join(out, "~" + format_estimate(n.task.estimate));
    if (with_places) {
        std::string at;
        for (const auto& key : node_places(n)) at += (at.empty() ? "at " : ", ") + place_title(key);
        join(out, at);
    }
    return out;
}

// HTML's five, for the rich copy.
std::string html_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c;
        }
    }
    return o;
}

std::string pct(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            o += static_cast<char>(c);
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}

std::string ics_date(std::int64_t when) { return strf("%Y%m%d", when); }
std::string ics_local(std::int64_t when) { return strf("%Y%m%dT%H%M%S", when); }
std::string ics_utc(std::int64_t when) {
    const std::time_t t = static_cast<std::time_t>(when);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

}  // namespace

const char* glance_head(GlanceKind k) {
    switch (k) {
        case GlanceKind::Late:     return "Late";
        case GlanceKind::DueToday: return "Due today";
        case GlanceKind::Short:    return "Running short";
        case GlanceKind::Starts:   return "Starts today";
        case GlanceKind::Flagged:  return "Flagged";
        case GlanceKind::Errands:  return "Errands";
    }
    return "";
}

int Glance::count() const {
    int c = 0;
    for (const auto& s : sections) c += static_cast<int>(s.items.size());
    return c;
}

Glance glance(const NodeSource& src, const TaskIndex& tasks, std::int64_t now) {
    Glance g;
    g.day   = day_start(now);
    g.title = "jot — " + short_day(now);
    const std::int64_t tonight = day_end(now);

    std::set<NodeId> shown;
    auto add = [&](GlanceSection& sec, const Node& n, std::string detail) {
        if (!shown.insert(n.id).second) return;
        GlanceItem it;
        it.id      = n.id;
        it.title   = n.title.empty() ? "(untitled)" : n.title;
        it.detail  = std::move(detail);
        it.due     = effective_due(src, n.id);
        it.minutes = n.task.estimate;
        sec.items.push_back(std::move(it));
    };
    auto keep = [&](GlanceSection&& sec) {
        if (!sec.items.empty()) g.sections.push_back(std::move(sec));
    };

    // Late
    {
        GlanceSection sec{GlanceKind::Late, glance_head(GlanceKind::Late), {}};
        for (const auto& id : tasks.query(src, Filter::Overdue, now)) {
            const Node* n = src.find(id);
            if (!n || !open_work(src, id, now)) continue;
            const std::int64_t due = effective_due(src, id);
            const int d = days_between(due, now);
            std::string w = d <= 0 ? "was due " + format_clock(due)
                          : d == 1 ? "1 day late"
                                   : std::to_string(d) + " days late";
            join(w, tail(src, *n, true, true));
            add(sec, *n, w);
        }
        keep(std::move(sec));
    }
    // Due today -- what is left of it
    {
        GlanceSection sec{GlanceKind::DueToday, glance_head(GlanceKind::DueToday), {}};
        for (const auto& id : tasks.tasks()) {
            const Node* n = src.find(id);
            if (!n || !open_work(src, id, now)) continue;
            const std::int64_t due = effective_due(src, id);
            if (due == 0 || due < now || due > tonight) continue;
            std::string w = due == day_end(due) ? std::string{} : "by " + format_clock(due);
            join(w, tail(src, *n, true, true));
            add(sec, *n, w);
        }
        keep(std::move(sec));
    }
    // Running short -- a deadline's next step
    {
        GlanceSection sec{GlanceKind::Short, glance_head(GlanceKind::Short), {}};
        for (const auto& p : deadline_pulls(src, now)) {
            const Node* step = src.find(p.step);
            const Node* dl   = src.find(p.deadline);
            if (!step || !dl) continue;
            std::string w;
            if (p.step != p.deadline) join(w, "for " + dl->title);
            const std::int64_t due = effective_due(src, p.deadline);
            if (due) join(w, (due < now ? "was due " : "due ") + short_day(due));
            join(w, tail(src, *step, false, true));
            add(sec, *step, w);
        }
        keep(std::move(sec));
    }
    // Starts today
    {
        GlanceSection sec{GlanceKind::Starts, glance_head(GlanceKind::Starts), {}};
        for (const auto& id : tasks.tasks()) {
            const Node* n = src.find(id);
            if (!n || !open_work(src, id, now)) continue;
            const std::int64_t defer = effective_defer(src, id);
            if (defer == 0 || defer < g.day || defer > tonight) continue;
            std::string w = defer == g.day ? std::string{} : "from " + format_clock(defer);
            join(w, tail(src, *n, true, true));
            add(sec, *n, w);
        }
        keep(std::move(sec));
    }
    // Flagged
    {
        GlanceSection sec{GlanceKind::Flagged, glance_head(GlanceKind::Flagged), {}};
        for (const auto& id : tasks.query(src, Filter::Flagged, now)) {
            const Node* n = src.find(id);
            if (!n || !open_work(src, id, now)) continue;
            std::string w;
            if (const std::int64_t due = effective_due(src, id)) join(w, "due " + short_day(due));
            join(w, tail(src, *n, true, true));
            add(sec, *n, w);
        }
        keep(std::move(sec));
    }
    // Errands, one section per place -- only what is doable and not said above
    for (const auto& run : errand_runs(src, now)) {
        GlanceSection sec{GlanceKind::Errands, run.name, {}};
        for (const auto& e : run.errands) {
            const Node* n = src.find(e.id);
            if (!n) continue;
            std::string w = e.sub;
            if (const std::int64_t due = effective_due(src, e.id)) join(w, "due " + short_day(due));
            join(w, tail(src, *n, true, false));
            add(sec, *n, w);
        }
        keep(std::move(sec));
    }

    // The line under the title
    int late = 0, minutes = 0, unsized = 0;
    for (const auto& s : g.sections)
        for (const auto& it : s.items) {
            if (s.kind == GlanceKind::Late) ++late;
            if (it.minutes > 0) minutes += it.minutes; else ++unsized;
        }
    const int c = g.count();
    if (c == 0) {
        g.summary = "Nothing on today.";
    } else {
        g.summary = c == 1 ? "1 thing" : std::to_string(c) + " things";
        if (minutes > 0) join(g.summary, "~" + format_estimate(minutes));
        if (late > 0) join(g.summary, std::to_string(late) + " late");
    }
    (void)unsized;
    return g;
}

// ── renderings ──────────────────────────────────────────────────────────────

std::string glance_text(const Glance& g) {
    std::string o = g.title + "\n" + g.summary + "\n";
    bool errands_head = false;
    for (const auto& s : g.sections) {
        o += "\n";
        if (s.kind == GlanceKind::Errands) {
            if (!errands_head) { o += "ERRANDS\n"; errands_head = true; }
            o += s.head + ":\n";
        } else {
            std::string h = s.head;
            std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return std::toupper(c); });
            o += h + "\n";
        }
        for (const auto& it : s.items) {
            o += "• " + it.title;
            if (!it.detail.empty()) o += " — " + it.detail;
            o += "\n";
        }
    }
    return o;
}

std::string glance_html(const Glance& g) {
    std::string o = "<meta charset=\"utf-8\"><div>";
    o += "<h3>" + html_escape(g.title) + "</h3>";
    o += "<p><i>" + html_escape(g.summary) + "</i></p>";
    bool errands_head = false;
    for (const auto& s : g.sections) {
        if (s.kind == GlanceKind::Errands) {
            if (!errands_head) { o += "<h4>Errands</h4>"; errands_head = true; }
            o += "<p><b>" + html_escape(s.head) + "</b></p>";
        } else {
            o += "<h4>" + html_escape(s.head) + "</h4>";
        }
        o += "<ul>";
        for (const auto& it : s.items) {
            o += "<li><b>" + html_escape(it.title) + "</b>";
            if (!it.detail.empty()) o += " — " + html_escape(it.detail);
            o += "</li>";
        }
        o += "</ul>";
    }
    o += "</div>";
    return o;
}

std::string ics_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case ';':  o += "\\;"; break;
            case ',':  o += "\\,"; break;
            case '\n': o += "\\n"; break;
            case '\r': break;
            default:   o += c;
        }
    }
    return o;
}

std::string ics_fold(const std::string& line) {
    // 75 octets a line; never split a UTF-8 sequence (continuation bytes are
    // 10xxxxxx). Continuations start with one space, which counts.
    std::string o;
    std::size_t i = 0, width = 75;
    while (line.size() - i > width) {
        std::size_t cut = i + width;
        while (cut > i && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) --cut;
        // Nor an escape ("\n", "\,"): legal to split, but naive readers trip.
        std::size_t bs = 0;
        while (cut - bs > i && line[cut - bs - 1] == '\\') ++bs;
        if (bs % 2 == 1) --cut;
        o += line.substr(i, cut - i) + "\r\n ";
        i = cut;
        width = 74;
    }
    o += line.substr(i) + "\r\n";
    return o;
}

std::string glance_ics_name(const Glance& g) {
    return "jot-glance-" + strf("%Y-%m-%d", g.day) + ".ics";
}

std::string glance_ics(const Glance& g, std::int64_t stamp) {
    const std::string date = ics_date(g.day);
    const std::string dtstamp = ics_utc(stamp);
    std::string o;
    auto line = [&](const std::string& l) { o += ics_fold(l); };
    line("BEGIN:VCALENDAR");
    line("VERSION:2.0");
    line("PRODID:-//jot//Glance at Today//EN");
    line("CALSCALE:GREGORIAN");
    line("METHOD:PUBLISH");

    // The day's card: all day, free time, the whole list in its notes. A fixed
    // UID per day, so importing the day again replaces rather than doubles.
    std::string body = glance_text(g);
    if (auto p = body.find('\n'); p != std::string::npos) body.erase(0, p + 1);   // the title is the SUMMARY
    while (!body.empty() && body.back() == '\n') body.pop_back();
    line("BEGIN:VEVENT");
    line("UID:jot-glance-" + date + "@scott8420.github.io");
    line("DTSTAMP:" + dtstamp);
    line("DTSTART;VALUE=DATE:" + date);
    line("DTEND;VALUE=DATE:" + ics_date(next_day(g.day)));
    line("SUMMARY:" + ics_escape(g.title + " (" + g.summary + ")"));
    line("DESCRIPTION:" + ics_escape(body));
    line("TRANSP:TRANSPARENT");
    line("END:VEVENT");

    // Each thing due at a real hour today: a slot ending at its due, as long
    // as its estimate (15 minutes when unsized), in local (floating) time.
    for (const auto& s : g.sections) {
        if (s.kind != GlanceKind::DueToday) continue;
        for (const auto& it : s.items) {
            if (it.due == 0 || it.due == day_end(it.due)) continue;
            const int mins = it.minutes > 0 ? it.minutes : 15;
            line("BEGIN:VEVENT");
            line("UID:jot-" + it.id + "-" + date + "@scott8420.github.io");
            line("DTSTAMP:" + dtstamp);
            line("DTSTART:" + ics_local(it.due - mins * 60));
            line("DTEND:" + ics_local(it.due));
            line("SUMMARY:" + ics_escape(it.title));
            if (!it.detail.empty()) line("DESCRIPTION:" + ics_escape(it.detail));
            line("END:VEVENT");
        }
    }
    line("END:VCALENDAR");
    return o;
}

std::string glance_mailto(const Glance& g, const std::string& to) {
    std::string body = glance_text(g);
    std::string crlf;
    for (char c : body) {
        if (c == '\n') crlf += "\r\n"; else crlf += c;
    }
    std::string addr;
    for (char c : to) if (!std::isspace(static_cast<unsigned char>(c))) addr += c;   // an address, as typed
    return "mailto:" + addr + "?subject=" + pct(g.title) + "&body=" + pct(crlf);
}

// ── s071: jot --glance ─────────────────────────────────────────────────────

std::string glance_cli(const Glance& g, bool ics, std::int64_t now) {
    if (ics) return glance_ics(g, now);
    std::string t = glance_text(g);
    if (!t.empty() && t.back() != '\n') t += '\n';
    return t;
}

bool glance_of_folder(const std::string& dir, std::int64_t now, bool ics,
                      std::string& out, std::string& err) {
    out.clear();
    err.clear();
    Project jots;
    if (!jots.open(dir)) {
        err = "cannot open the jots folder " + dir;
        return false;
    }
    TaskIndex tasks;
    tasks.rebuild(jots);
    out = glance_cli(glance(jots, tasks, now), ics, now);
    return true;
}

bool glance_command(const std::vector<std::string>& argv, const std::string& data_dir,
                    std::int64_t now, std::string& out, std::string& err, int& rc) {
    out.clear();
    err.clear();
    rc = 0;
    bool asked = false, ics = false;
    std::vector<std::string> rest;
    for (std::size_t i = 1; i < argv.size(); ++i) {
        if (argv[i] == "--glance") asked = true;
        else if (argv[i] == "ics" || argv[i] == "--ics") ics = true;
        else rest.push_back(argv[i]);
    }
    if (!asked) return false;
    if (!rest.empty()) {
        err = "usage: jot --glance [ics]\n"
              "  the day on one card, as text -- or, with ics, as a calendar file\n";
        rc = 2;
        return true;
    }
    const std::string base = data_dir + "/jot/";
    const Prefs prefs = load_prefs(base + "prefs.json");
    const std::vector<std::string> recents = load_recents(base + "recent.json");
    if (recents.empty() || prefs.jots_closed) {
        err = "jot: no jots folder open -- nothing to glance at.\n";
        rc = 1;
        return true;
    }
    if (!glance_of_folder(recents.front(), now, ics, out, err)) {
        err = "jot: " + err + "\n";
        rc = 1;
    }
    return true;
}

}  // namespace jot::core
