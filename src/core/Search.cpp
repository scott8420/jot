#include "core/Search.hpp"
#include "core/Review.hpp"
#include "core/Tags.hpp"
#include "core/Tasks.hpp"

#include <algorithm>
#include <ctime>
#include <functional>
#include <sstream>

// core/Search.cpp -- see the header. ASCII case-folding: the byte-wise lower
// keeps UTF-8 offsets valid and matches the cheat sheet's search; a word in
// another script matches as typed.

namespace jot::core {
namespace {

std::string lower(const std::string& s) {
    std::string o = s;
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return o;
}

int cp_count(const std::string& s, std::size_t from, std::size_t to) {
    int n = 0;
    for (std::size_t i = from; i < to && i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    return n;
}

// Back off to a codepoint boundary, so a cut never splits a character.
std::size_t boundary(const std::string& s, std::size_t i) {
    while (i > 0 && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return i;
}

// The line holding byte `at`, trimmed, cut to about `width` bytes around the
// hit so the matched word is always in view.
std::string snippet_at(const std::string& body, std::size_t at, std::size_t width = 90) {
    std::size_t b = body.rfind('\n', at);
    b = (b == std::string::npos) ? 0 : b + 1;
    std::size_t e = body.find('\n', at);
    if (e == std::string::npos) e = body.size();
    while (b < e && (body[b] == ' ' || body[b] == '\t')) ++b;
    while (e > b && (body[e - 1] == ' ' || body[e - 1] == '\t' || body[e - 1] == '\r')) --e;
    std::string pre, post;
    if (e - b > width) {
        std::size_t from = at > b + width / 3 ? at - width / 3 : b;
        from = boundary(body, from);
        std::size_t to = boundary(body, std::min(e, from + width));
        if (from > b) pre = "…";
        if (to < e) post = "…";
        b = from;
        e = to;
    }
    return pre + body.substr(b, e - b) + post;
}

void walk(const NodeSource& src, const NodeId& parent,
          const std::function<void(const Node&)>& fn) {
    for (const auto& id : src.children(parent)) {
        if (const Node* n = src.find(id)) fn(*n);
        walk(src, id, fn);
    }
}

// ── the query ───────────────────────────────────────────────────────────────

struct Named { const char* word; StateTerm state; };
constexpr Named kStates[] = {
    {"remaining", StateTerm::Remaining}, {"available", StateTerm::Available},
    {"waiting", StateTerm::Waiting},     {"done", StateTerm::Done},
    {"dropped", StateTerm::Dropped},     {"todo", StateTerm::Todo},
    {"note", StateTerm::Note},           {"project", StateTerm::Project},
    {"inbox", StateTerm::Inbox},         {"flagged", StateTerm::Flagged},
};
struct NamedDue { const char* word; DueTerm due; };
constexpr NamedDue kDues[] = {
    {"overdue", DueTerm::Overdue}, {"today", DueTerm::Today}, {"week", DueTerm::Week},
    {"any", DueTerm::Any},         {"none", DueTerm::None},
};

std::vector<std::string> tokens(const std::string& q) {
    std::vector<std::string> out;
    std::istringstream in(q);
    for (std::string t; in >> t;) out.push_back(t);
    return out;
}

// One token as a term (without its minus). Lower-cased first.
Term term_of(const std::string& raw) {
    Term t;
    const std::string w = lower(raw);
    if (w.size() > 1 && w[0] == '#') {
        const std::string key = tag_key(std::string_view(w).substr(1));
        if (!key.empty()) {
            t.kind = TermKind::Tag;
            t.text = key;
            return t;
        }
    }
    if (w.rfind("is:", 0) == 0)
        for (const auto& s : kStates)
            if (w.substr(3) == s.word) {
                t.kind = TermKind::State;
                t.state = s.state;
                t.text = w;
                return t;
            }
    if (w.rfind("est:", 0) == 0 && w.size() > 4) {   // s040
        const std::string v = w.substr(4);
        const int m = v == "none" ? 0 : parse_estimate(v);
        if (m >= 0 && (m > 0 || v == "none")) {
            t.kind = TermKind::Est;
            t.minutes = m;
            t.text = w;
            return t;
        }
    }
    if (w.rfind("due:", 0) == 0)
        for (const auto& d : kDues)
            if (w.substr(4) == d.word) {
                t.kind = TermKind::Due;
                t.due = d.due;
                t.text = w;
                return t;
            }
    t.text = w;
    return t;
}

bool remaining(Avail a) { return a != Avail::NotTask && a != Avail::Done && a != Avail::Dropped; }

// Per-node facts, worked out once and only when a term asks.
struct Facts {
    const NodeSource& src;
    const Node& n;
    std::int64_t now;
    std::string name, body;   // lower-cased
    bool have_avail = false;
    Avail avail = Avail::NotTask;
    bool have_tags = false;
    std::vector<std::string> tags;

    Avail av() {
        if (!have_avail) { avail = availability(src, n.id, now); have_avail = true; }
        return avail;
    }
    const std::vector<std::string>& tag_keys() {
        if (!have_tags) { tags = node_tag_keys(n); have_tags = true; }
        return tags;
    }
};

bool term_holds(Facts& f, const Term& t) {
    switch (t.kind) {
    case TermKind::Word:
        return f.name.find(t.text) != std::string::npos || f.body.find(t.text) != std::string::npos;
    case TermKind::Tag:
        for (const auto& k : f.tag_keys())
            if (k.rfind(t.text, 0) == 0) return true;   // equal, nested beneath, or typed so far
        return false;
    case TermKind::State:
        switch (t.state) {
        case StateTerm::Remaining: return remaining(f.av());
        case StateTerm::Available: return f.av() == Avail::Available;
        case StateTerm::Waiting: {
            const Avail a = f.av();
            return a == Avail::Deferred || a == Avail::Blocked || a == Avail::OnHold;
        }
        case StateTerm::Done:    return f.av() == Avail::Done;
        case StateTerm::Dropped: return f.av() == Avail::Dropped;
        case StateTerm::Todo:    return f.n.task.is_task;
        case StateTerm::Note:    return !f.n.task.is_task;
        case StateTerm::Project: return is_project(f.src, f.n.id);
        case StateTerm::Inbox:   return f.n.inbox;
        case StateTerm::Flagged:
            // A todo: flagged (or in a flagged project) and still to do. A
            // note: its own flag -- a project said on purpose can carry one.
            return f.n.task.is_task ? (remaining(f.av()) && effective_flagged(f.src, f.n.id))
                                    : f.n.task.flagged;
        }
        return false;
    case TermKind::Est: {
        if (!f.n.task.is_task || !remaining(f.av())) return false;
        const int e = f.n.task.estimate;
        return t.minutes == 0 ? e == 0 : (e > 0 && e <= t.minutes);
    }
    case TermKind::Due: {
        if (!f.n.task.is_task || !remaining(f.av())) return false;
        const std::int64_t due = effective_due(f.src, f.n.id);
        switch (t.due) {
        case DueTerm::Overdue: return due != 0 && due < f.now;
        case DueTerm::Today:   return due != 0 && due <= day_end(f.now);
        case DueTerm::Week:    return due != 0 && due <= day_end(f.now + 6 * 86400);
        case DueTerm::Any:     return due != 0;
        case DueTerm::None:    return due == 0;
        }
        return false;
    }
    }
    return false;
}

bool holds(Facts& f, const Term& t) { return term_holds(f, t) != t.negate; }

bool match_query(const NodeSource& src, const Node& n, const Query& q, std::int64_t now,
                 SearchHit* hit) {
    Facts f{src, n, now, lower(n.title), lower(n.body)};
    for (const auto& clause : q.clauses) {
        bool any = false;
        for (const auto& t : clause)
            if (holds(f, t)) { any = true; break; }
        if (!any) return false;
    }
    if (!hit) return true;

    // Which positive words and tags are here -- for the bold name and the line.
    hit->id = n.id;
    bool words = false, all_in_name = true;
    std::size_t first = std::string::npos, first_len = 0;
    for (const auto& clause : q.clauses)
        for (const auto& t : clause) {
            if (t.negate) continue;
            std::string needle;
            if (t.kind == TermKind::Word) needle = t.text;
            else if (t.kind == TermKind::Tag) needle = "#" + t.text;
            else continue;
            const bool in_name = t.kind == TermKind::Word && f.name.find(needle) != std::string::npos;
            const std::size_t at = f.body.find(needle);
            if (!in_name && at == std::string::npos) continue;   // an or-ed term that is not here
            if (t.kind == TermKind::Word) {
                words = true;
                all_in_name = all_in_name && in_name;
            }
            if (at != std::string::npos && (first == std::string::npos || at < first)) {
                first = at;
                first_len = needle.size();
            }
        }
    hit->in_name = words && all_in_name;
    if (first != std::string::npos) {
        hit->snippet  = snippet_at(n.body, first);
        hit->cp_start = cp_count(n.body, 0, first);
        hit->cp_len   = cp_count(n.body, first, first + first_len);
    }
    return true;
}

// The line a filter-only hit shows: a todo says where it stands; a note, its
// first line of text.
std::string short_day(std::int64_t when) {
    std::time_t t = static_cast<std::time_t>(when);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%a %d %b", &tm);
    return buf;
}

std::string due_words(std::int64_t due, std::int64_t now) {
    if (due == 0) return {};
    if (due < now) return due >= day_start(now) ? "overdue (today)" : "overdue since " + short_day(due);
    if (due <= day_end(now)) return "due today";
    if (due <= day_end(now + 86400)) return "due tomorrow";
    return "due " + short_day(due);
}

std::string status_line(const NodeSource& src, const Node& n, std::int64_t now) {
    if (n.task.is_task) {
        std::string s = avail_name(availability(src, n.id, now));
        const std::string d = due_words(effective_due(src, n.id), now);
        if (!d.empty()) s += " · " + d;
        if (n.task.estimate > 0) s += " · " + format_estimate(n.task.estimate);
        if (effective_flagged(src, n.id)) s += " · flagged";
        return s;
    }
    std::size_t b = 0;
    while (b < n.body.size()) {
        std::size_t e = n.body.find('\n', b);
        if (e == std::string::npos) e = n.body.size();
        std::string line = n.body.substr(b, e - b);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
        if (!line.empty()) return snippet_at(line, 0);
        b = e + 1;
    }
    return {};
}

std::int64_t clock_or(std::int64_t now) {
    return now != 0 ? now : static_cast<std::int64_t>(std::time(nullptr));
}

// The plain tokens a menu reads and writes: not or-ed, not negated.
bool plain_of_kind(const std::vector<std::string>& toks, std::size_t i, TermKind kind,
                   bool flagged) {
    const std::string& raw = toks[i];
    if (raw.empty() || raw[0] == '-') return false;
    if (lower(raw) == "or") return false;
    if (i > 0 && lower(toks[i - 1]) == "or") return false;
    if (i + 1 < toks.size() && lower(toks[i + 1]) == "or") return false;
    const Term t = term_of(raw);
    if (t.kind != kind) return false;
    if (kind == TermKind::State) return (t.state == StateTerm::Flagged) == flagged;
    return true;
}

std::string first_plain(const std::string& q, TermKind kind, std::size_t prefix) {
    const auto toks = tokens(q);
    for (std::size_t i = 0; i < toks.size(); ++i)
        if (plain_of_kind(toks, i, kind, false)) return lower(toks[i]).substr(prefix);
    return {};
}

std::string rewrite(const std::string& q, TermKind kind, bool flagged, const std::string& add) {
    const auto toks = tokens(q);
    std::string out;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        if (plain_of_kind(toks, i, kind, flagged)) continue;
        if (!out.empty()) out += ' ';
        out += toks[i];
    }
    if (!add.empty()) {
        if (!out.empty()) out += ' ';
        out += add;
    }
    return out;
}

std::string term_words(const Term& t) {
    std::string s;
    switch (t.kind) {
    case TermKind::Word: s = "“" + t.text + "”"; break;
    case TermKind::Tag:  s = "#" + t.text; break;
    case TermKind::State:
        switch (t.state) {
        case StateTerm::Remaining: s = "remaining todos"; break;
        case StateTerm::Available: s = "available todos"; break;
        case StateTerm::Waiting:   s = "waiting todos"; break;
        case StateTerm::Done:      s = "done"; break;
        case StateTerm::Dropped:   s = "dropped"; break;
        case StateTerm::Todo:      s = "todos"; break;
        case StateTerm::Note:      s = "notes (not todos)"; break;
        case StateTerm::Project:   s = "projects"; break;
        case StateTerm::Inbox:     s = "in the Inbox"; break;
        case StateTerm::Flagged:   s = "flagged"; break;
        }
        break;
    case TermKind::Est:
        s = t.minutes == 0 ? "no estimate" : format_estimate(t.minutes) + " or less";
        break;
    case TermKind::Due:
        switch (t.due) {
        case DueTerm::Overdue: s = "overdue"; break;
        case DueTerm::Today:   s = "due by today"; break;
        case DueTerm::Week:    s = "due within a week"; break;
        case DueTerm::Any:     s = "with a due date"; break;
        case DueTerm::None:    s = "no due date"; break;
        }
        break;
    }
    return t.negate ? "not " + s : s;
}

}  // namespace

bool Query::has_filters() const {
    for (const auto& c : clauses) {
        if (c.size() > 1) return true;
        for (const auto& t : c)
            if (t.negate || t.kind != TermKind::Word) return true;
    }
    return false;
}

Query parse_query(const std::string& query) {
    Query q;
    bool join = false;   // the last token was `or`
    for (const auto& raw : tokens(query)) {
        if (lower(raw) == "or") {
            join = !q.clauses.empty();   // a leading `or` joins nothing
            continue;
        }
        Term t;
        if (raw.size() > 1 && raw[0] == '-') {
            t = term_of(raw.substr(1));
            t.negate = true;
        } else {
            t = term_of(raw);
        }
        if (join) q.clauses.back().push_back(std::move(t));
        else      q.clauses.push_back({std::move(t)});
        join = false;
    }
    return q;
}

bool query_active(const std::string& query) { return !parse_query(query).empty(); }

bool query_matches(const NodeSource& src, const Node& n, const Query& q, std::int64_t now) {
    return !q.empty() && match_query(src, n, q, now, nullptr);
}

std::string describe_query(const std::string& query) {
    const Query q = parse_query(query);
    if (!q.has_filters()) return {};
    std::string out;
    for (const auto& c : q.clauses) {
        std::string part;
        for (const auto& t : c) {
            if (!part.empty()) part += " or ";
            part += term_words(t);
        }
        if (!out.empty()) out += " · ";
        out += part;
    }
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

std::string query_show(const std::string& query)   { return first_plain(query, TermKind::State, 3); }
std::string query_due(const std::string& query)    { return first_plain(query, TermKind::Due, 4); }
bool query_flagged(const std::string& query) {
    const auto toks = tokens(query);
    for (std::size_t i = 0; i < toks.size(); ++i)
        if (plain_of_kind(toks, i, TermKind::State, true)) return true;
    return false;
}
std::string query_set_show(const std::string& query, const std::string& show) {
    return rewrite(query, TermKind::State, false, show.empty() ? "" : "is:" + show);
}
std::string query_est(const std::string& query) {
    const std::string v = first_plain(query, TermKind::Est, 4);
    if (v.empty() || v == "none") return v;
    return std::to_string(parse_estimate(v));   // "1h" reads as "60", the menu's target
}
std::string query_set_est(const std::string& query, const std::string& est) {
    return rewrite(query, TermKind::Est, false, est.empty() ? "" : "est:" + est);
}
std::string query_set_due(const std::string& query, const std::string& due) {
    return rewrite(query, TermKind::Due, false, due.empty() ? "" : "due:" + due);
}
std::string query_set_flagged(const std::string& query, bool on) {
    return rewrite(query, TermKind::State, true, on ? "is:flagged" : "");
}

std::vector<std::string> search_words(const std::string& query) {
    std::vector<std::string> out;
    std::istringstream in(lower(query));
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::vector<SearchHit> search(const NodeSource& src, const std::string& query, std::size_t cap,
                              std::int64_t now) {
    const Query q = parse_query(query);
    if (q.empty()) return {};
    now = clock_or(now);
    std::vector<SearchHit> names, rest;
    walk(src, "", [&](const Node& n) {
        SearchHit h;
        if (!match_query(src, n, q, now, &h)) return;
        if (h.snippet.empty() && !h.in_name) h.snippet = status_line(src, n, now);
        (h.in_name ? names : rest).push_back(std::move(h));
    });
    names.insert(names.end(), std::make_move_iterator(rest.begin()),
                 std::make_move_iterator(rest.end()));
    if (names.size() > cap) names.resize(cap);   // ranked first, then cut: a name hit is never lost to the cap
    return names;
}

std::size_t search_count(const NodeSource& src, const std::string& query, std::int64_t now) {
    const Query q = parse_query(query);
    if (q.empty()) return 0;
    now = clock_or(now);
    std::size_t n = 0;
    walk(src, "", [&](const Node& node) {
        if (match_query(src, node, q, now, nullptr)) ++n;
    });
    return n;
}

// ── perspectives ────────────────────────────────────────────────────────────

std::string query_norm(const std::string& query) {
    std::string out;
    for (const auto& t : tokens(lower(query))) {
        if (!out.empty()) out += ' ';
        out += t;
    }
    return out;
}

const Perspective* perspective_for(const std::vector<Perspective>& list, const std::string& query) {
    const std::string k = query_norm(query);
    if (k.empty()) return nullptr;
    for (const auto& p : list)
        if (query_norm(p.query) == k) return &p;
    return nullptr;
}

const Perspective* perspective_named(const std::vector<Perspective>& list, const std::string& name) {
    const std::string k = query_norm(name);
    for (const auto& p : list)
        if (query_norm(p.name) == k) return &p;
    return nullptr;
}

bool perspective_save(std::vector<Perspective>& list, const std::string& name,
                      const std::string& query) {
    std::string n = name;
    while (!n.empty() && std::isspace(static_cast<unsigned char>(n.front()))) n.erase(n.begin());
    while (!n.empty() && std::isspace(static_cast<unsigned char>(n.back()))) n.pop_back();
    if (n.empty() || !query_active(query)) return false;
    perspective_remove(list, n);
    std::string q;
    for (const auto& t : tokens(query)) {
        if (!q.empty()) q += ' ';
        q += t;
    }
    list.push_back({n, q});
    std::stable_sort(list.begin(), list.end(), [](const Perspective& a, const Perspective& b) {
        return lower(a.name) < lower(b.name);
    });
    return true;
}

bool perspective_remove(std::vector<Perspective>& list, const std::string& name) {
    const std::string k = query_norm(name);
    const auto before = list.size();
    std::erase_if(list, [&](const Perspective& p) { return query_norm(p.name) == k; });
    return list.size() != before;
}

}  // namespace jot::core
