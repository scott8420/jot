#include "core/Tags.hpp"
#include "core/Markdown.hpp"

#include <algorithm>
#include <map>
#include <set>

// Tags.cpp -- see the header. One walk of the tree in document order, one scan
// per body; nothing cached, so nothing can go stale.

namespace jot::core {

namespace {

// Depth first, in the order the tree draws its rows.
template <class F>
void walk(const NodeSource& src, F&& visit) {
    std::vector<NodeId> stack;
    auto push_kids = [&](const NodeId& parent) {
        auto kids = src.children(parent);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    };
    push_kids("");
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (const Node* n = src.find(id)) visit(*n);
        push_kids(id);
    }
}

bool finished(Avail a) { return a == Avail::Done || a == Avail::Dropped; }

}  // namespace

std::string tag_key(std::string_view name) {
    std::string k(name);
    for (auto& c : k)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    while (!k.empty() && k.back() == '/') k.pop_back();
    return k;
}

bool tag_under(std::string_view key, std::string_view filter) {
    if (filter.empty()) return false;
    if (key.size() < filter.size()) return false;
    if (key.compare(0, filter.size(), filter) != 0) return false;
    return key.size() == filter.size() || key[filter.size()] == '/';
}

std::string tag_query(std::string_view typed) {
    std::size_t b = 0, e = typed.size();
    while (b < e && (typed[b] == ' ' || typed[b] == '\t' || typed[b] == '#')) ++b;
    while (e > b && (typed[e - 1] == ' ' || typed[e - 1] == '\t')) --e;
    std::string out;
    out.reserve(e - b);
    for (std::size_t i = b; i < e; ++i) {
        const char c = typed[i];
        out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

bool tag_matches(std::string_view key, std::string_view query) {
    return query.empty() || key.find(query) != std::string_view::npos;
}

std::string tag_pick(const std::vector<TagInfo>& list, std::string_view typed) {
    const std::string q = tag_query(typed);
    if (q.empty()) return {};
    const TagInfo* lone = nullptr;
    std::size_t hits = 0;
    for (const auto& t : list) {
        if (t.key == q) return t.key;          // exact wins over "several"
        if (tag_matches(t.key, q)) { lone = &t; ++hits; }
    }
    return hits == 1 ? lone->key : std::string();
}

std::vector<std::string> node_tag_keys(const Node& n) {
    std::vector<std::string> out;
    if (n.body.find('#') == std::string::npos) return out;   // the common case, free
    for (const auto& t : scan(n.body).tags) {
        std::string k = tag_key(t.name);
        if (k.empty()) continue;
        if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(std::move(k));
    }
    return out;
}

std::vector<TagInfo> tag_list(const NodeSource& src, std::int64_t now) {
    struct Acc {
        TagInfo           info;
        std::set<NodeId>  seen;   // distinct nodes, so a nested pair in one note counts once
    };
    std::map<std::string, Acc> acc;   // sorted by key: "home" before "home/garden"

    walk(src, [&](const Node& n) {
        if (n.body.find('#') == std::string::npos) return;
        // The spelling as written, keyed by fold, for the display name.
        std::vector<std::pair<std::string, std::string>> tagged;   // key, spelling
        for (const auto& t : scan(n.body).tags) {
            std::string k = tag_key(t.name);
            if (k.empty()) continue;
            bool dup = false;
            for (const auto& p : tagged) dup = dup || p.first == k;
            if (!dup) {
                std::string spelt = t.name;
                while (!spelt.empty() && spelt.back() == '/') spelt.pop_back();
                tagged.emplace_back(std::move(k), std::move(spelt));
            }
        }
        if (tagged.empty()) return;

        Avail a = Avail::NotTask;
        if (n.task.is_task) a = availability(src, n.id, now);

        for (const auto& [key, spelt] : tagged) {
            // The tag itself and every parent level it implies.
            std::size_t cut = key.size();
            bool own = true;
            while (true) {
                const std::string k = key.substr(0, cut);
                auto& slot = acc[k];
                if (slot.info.key.empty()) {
                    slot.info.key     = k;
                    slot.info.name    = spelt.substr(0, cut);
                    slot.info.depth   = static_cast<int>(std::count(k.begin(), k.end(), '/'));
                    slot.info.written = false;
                }
                if (own) slot.info.written = true;
                if (slot.seen.insert(n.id).second) {
                    if (!n.task.is_task)        ++slot.info.notes;
                    else if (!finished(a)) {
                        ++slot.info.open;
                        if (a == Avail::Available) ++slot.info.available;
                    }
                }
                const auto slash = k.rfind('/');
                if (slash == std::string::npos) break;
                cut = slash;
                own = false;
            }
        }
    });

    std::vector<TagInfo> out;
    out.reserve(acc.size());
    for (auto& [k, a] : acc) out.push_back(std::move(a.info));
    return out;
}

TagMembers tag_members(const NodeSource& src, std::string_view filter, std::int64_t now) {
    TagMembers m;
    const std::string f = tag_key(filter);
    if (f.empty()) return m;
    walk(src, [&](const Node& n) {
        bool hit = false;
        for (const auto& k : node_tag_keys(n)) hit = hit || tag_under(k, f);
        if (!hit) return;
        if (!n.task.is_task) { m.notes.push_back(n.id); return; }
        const Avail a = availability(src, n.id, now);
        if (finished(a))               { m.done.push_back(n.id); ++m.finished; }
        else if (a == Avail::Available) m.available.push_back(n.id);
        else                            m.waiting.push_back(n.id);
    });
    return m;
}

std::string waiting_reason(const NodeSource& src, const NodeId& id, std::int64_t now) {
    switch (availability(src, id, now)) {
        case Avail::Deferred: {
            const std::int64_t d = effective_defer(src, id);
            return d ? "Deferred until " + format_date(d) : std::string("Deferred");
        }
        case Avail::Blocked: return "Blocked: an earlier step is still open";
        case Avail::OnHold:  return "On hold";
        default:             return {};
    }
}

// ── the tag line (s035b) ────────────────────────────────────────────────────
namespace {

int cp_at(const std::string& s, std::size_t byte) {
    int cp = 0;
    for (std::size_t i = 0; i < byte && i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++cp;
    return cp;
}

bool blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

}  // namespace

TagLine find_tag_line(const std::string& body) {
    TagLine tl;
    const Scan sc = scan(body);
    int L = -1;
    for (int i = static_cast<int>(sc.lines.size()) - 1; i >= 0; --i) {
        const auto& ln = sc.lines[static_cast<std::size_t>(i)];
        bool any = false;
        for (int b = ln.begin; b < ln.end && !any; ++b) any = !blank(body[static_cast<std::size_t>(b)]);
        if (any) { L = i; break; }
    }
    if (L < 0) return tl;
    const auto& ln = sc.lines[static_cast<std::size_t>(L)];
    std::vector<bool> covered(static_cast<std::size_t>(ln.end - ln.begin), false);
    std::vector<std::string> names;
    for (const auto& t : sc.tags) {
        if (t.line != L) continue;
        names.push_back(t.name);
        for (int b = t.begin; b < t.end; ++b) covered[static_cast<std::size_t>(b - ln.begin)] = true;
    }
    if (names.empty()) return tl;
    for (int b = ln.begin; b < ln.end; ++b)
        if (!covered[static_cast<std::size_t>(b - ln.begin)] && !blank(body[static_cast<std::size_t>(b)]))
            return tl;   // something that is not a tag: an ordinary line that mentions one
    tl.found = true;
    tl.line = L;
    tl.begin = ln.begin;
    tl.end = ln.end;
    tl.cp_begin = ln.cp_begin;
    tl.cp_end = ln.cp_end;
    tl.names = std::move(names);
    return tl;
}

std::string clean_tag_name(std::string_view typed) {
    std::string s(typed);
    while (!s.empty() && blank(s.front())) s.erase(s.begin());
    while (!s.empty() && blank(s.back())) s.pop_back();
    while (!s.empty() && s.front() == '#') s.erase(s.begin());
    for (auto& c : s) if (c == ' ' || c == '\t') c = '-';
    while (!s.empty() && (s.back() == '/' || s.back() == '.' || s.back() == '-')) s.pop_back();
    if (s.empty()) return {};
    // The scanner is the judge: a name is a tag if "#name" scans as exactly
    // that one tag. One answer to "what is a tag", the editor's.
    const Scan sc = scan("#" + s);
    if (sc.tags.size() != 1 || sc.tags[0].name != s) return {};
    return s;
}

FmtEdit tag_add_edit(const std::string& body, std::string_view typed) {
    FmtEdit ed;
    const std::string name = clean_tag_name(typed);
    if (name.empty()) return ed;
    const std::string key = tag_key(name);
    for (const auto& t : scan(body).tags)
        if (tag_key(t.name) == key) return ed;   // already tagged, here or in the text

    const TagLine tl = find_tag_line(body);
    if (tl.found) {
        ed.cp_begin = ed.cp_end = tl.cp_end;
        ed.text = " #" + name;
    } else {
        // At the very end, after a blank line, so the line is a paragraph of
        // its own in any markdown reader rather than joined to the last one.
        std::size_t nl = 0;
        for (auto it = body.rbegin(); it != body.rend() && (*it == '\n' || *it == ' ' || *it == '\t' || *it == '\r'); ++it)
            if (*it == '\n') ++nl;
        const bool empty = body.find_first_not_of(" \t\r\n") == std::string::npos;
        ed.cp_begin = ed.cp_end = cp_at(body, body.size());
        ed.text = (empty ? std::string() : std::string(nl >= 2 ? 0 : 2 - nl, '\n')) + "#" + name;
        if (empty) ed.cp_begin = 0;   // a body of only whitespace: the line replaces it
    }
    ed.ok = true;
    ed.sel_begin = ed.sel_end = ed.cp_begin + cp_at(ed.text, ed.text.size());
    return ed;
}

FmtEdit tag_remove_edit(const std::string& body, std::string_view name) {
    FmtEdit ed;
    const TagLine tl = find_tag_line(body);
    if (!tl.found) return ed;
    const std::string key = tag_key(name);
    std::vector<std::string> keep;
    bool hit = false;
    for (const auto& n : tl.names) {
        if (tag_key(n) == key) hit = true;
        else keep.push_back(n);
    }
    if (!hit) return ed;
    ed.ok = true;
    if (!keep.empty()) {
        std::string line;
        for (const auto& n : keep) line += (line.empty() ? "#" : " #") + n;
        ed.cp_begin = tl.cp_begin;
        ed.cp_end = tl.cp_end;
        ed.text = line;
    } else {
        // The whole line goes, with the blank run before it and anything
        // blank after it: the note ends where its last real line ends.
        std::size_t from = static_cast<std::size_t>(tl.begin);
        while (from > 0 && blank(body[from - 1])) --from;
        ed.cp_begin = cp_at(body, from);
        ed.cp_end = cp_at(body, body.size());
        ed.text.clear();
    }
    ed.sel_begin = ed.sel_end = ed.cp_begin + cp_at(ed.text, ed.text.size());
    return ed;
}

}  // namespace jot::core
