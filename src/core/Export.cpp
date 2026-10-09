#include "core/Export.hpp"

#include "core/Repeat.hpp"
#include "core/Review.hpp"
#include "core/Tasks.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>

// core/Export.cpp -- see the header.

namespace jot::core {

namespace fs = std::filesystem;

namespace {

std::tm local_of(std::int64_t when) {
    const std::time_t t = static_cast<std::time_t>(when);
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm;
}

std::string fmt_tm(const std::tm& tm, const char* f) {
    char buf[64];
    const std::size_t n = std::strftime(buf, sizeof buf, f, &tm);
    return std::string(buf, n);
}

// The end of a day (23:59) is how jot writes "due that day, no hour".
bool no_hour(std::int64_t when) { return when == day_end(when) || when == day_start(when); }

// Front matter: "2026-10-09" or "2026-10-09 17:00".
std::string iso_when(std::int64_t when) {
    const std::tm tm = local_of(when);
    return no_hour(when) ? fmt_tm(tm, "%Y-%m-%d") : fmt_tm(tm, "%Y-%m-%d %H:%M");
}

// On a line: "Fri 9 Oct", "Fri 9 Oct 17:00", with the year when it is not now's.
std::string say_when(std::int64_t when, std::int64_t now) {
    const std::tm tm = local_of(when);
    const std::tm nt = local_of(now);
    std::string s = fmt_tm(tm, "%a ") + std::to_string(tm.tm_mday) + fmt_tm(tm, " %b");
    if (tm.tm_year != nt.tm_year) s += " " + std::to_string(1900 + tm.tm_year);
    if (!no_hour(when)) s += fmt_tm(tm, " %H:%M");
    return s;
}

// A YAML scalar that survives any title: quoted when it holds anything a
// parser could read as structure.
std::string yaml_str(const std::string& s) {
    const bool plain = !s.empty() &&
        s.find_first_of(":#[]{},&*!|>'\"%@`\n") == std::string::npos &&
        s.front() != ' ' && s.back() != ' ' && s.front() != '-' && s.front() != '?';
    if (plain) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out + "\"";
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= text.size()) {
        const std::size_t e = text.find('\n', i);
        if (e == std::string::npos) { out.push_back(text.substr(i)); break; }
        out.push_back(text.substr(i, e - i));
        i = e + 1;
    }
    while (!out.empty() && out.back().find_first_not_of(" \t") == std::string::npos) out.pop_back();
    while (!out.empty() && out.front().find_first_not_of(" \t") == std::string::npos) out.erase(out.begin());
    return out;
}

bool is_fence(const std::string& l) {
    const auto p = l.find_first_not_of(' ');
    return p != std::string::npos && p < 4 &&
           (l.compare(p, 3, "```") == 0 || l.compare(p, 3, "~~~") == 0);
}

// "## Lists" -> its level (2), 0 for a line that is not an ATX heading.
int heading_level(const std::string& l) {
    std::size_t n = 0;
    while (n < l.size() && l[n] == '#') ++n;
    if (n == 0 || n > 6) return 0;
    if (n < l.size() && l[n] != ' ') return 0;
    return static_cast<int>(n);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    const auto b = s.find_last_not_of(" \t#");
    return s.substr(a, b - a + 1);
}

// A note's text placed under a heading of `level`: its own headings pushed
// down by `level` (capped at 6), a first heading that just repeats the title
// dropped, code fences left alone.
std::vector<std::string> demoted_body(const std::string& body, const std::string& title, int level) {
    std::vector<std::string> ls = lines_of(body);
    if (!ls.empty()) {
        const int h = heading_level(ls.front());
        if (h > 0 && lower(trim(ls.front().substr(static_cast<std::size_t>(h)))) == lower(trim(title))) {
            ls.erase(ls.begin());
            while (!ls.empty() && ls.front().find_first_not_of(" \t") == std::string::npos) ls.erase(ls.begin());
        }
    }
    bool fence = false;
    for (auto& l : ls) {
        if (is_fence(l)) { fence = !fence; continue; }
        if (fence) continue;
        const int h = heading_level(l);
        if (h == 0) continue;
        const int to = std::min(6, h + level);
        l = std::string(static_cast<std::size_t>(to), '#') + l.substr(static_cast<std::size_t>(h));
    }
    return ls;
}

std::string url_path(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c >= 0x80) out += static_cast<char>(c);
        else {
            static const char* hex = "0123456789ABCDEF";
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// [label](jot:<id>) -> [label](File.md), or just "label" when the target was
// not exported.
std::string rewrite_links(const std::string& text, const std::map<NodeId, std::string>& file_of) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t m = text.find("](jot:", i);
        if (m == std::string::npos) { out += text.substr(i); break; }
        const std::size_t close = text.find(')', m + 6);
        const std::size_t open = text.rfind('[', m);
        if (close == std::string::npos || open == std::string::npos || open < i) {
            out += text.substr(i, m + 6 - i);
            i = m + 6;
            continue;
        }
        const std::string id = text.substr(m + 6, close - (m + 6));
        const std::string label = text.substr(open + 1, m - open - 1);
        out += text.substr(i, open - i);
        const auto it = file_of.find(id);
        if (it != file_of.end()) out += "[" + label + "](" + url_path(it->second) + ")";
        else out += label;
        i = close + 1;
    }
    return out;
}

std::string tag_words(const std::vector<std::string>& tags) {
    std::string s;
    for (const auto& t : tags) s += (s.empty() ? "#" : " #") + t;
    return s;
}

// The facts a todo line carries after its title.
std::string todo_facts(const Node& n, std::int64_t now) {
    std::vector<std::string> bits;
    const Task& t = n.task;
    if (t.done) bits.push_back(t.finished ? "done " + say_when(t.finished, now) : "done");
    if (t.due) bits.push_back((t.deadline ? "deadline " : "due ") + say_when(t.due, now));
    if (t.defer) bits.push_back("starts " + say_when(t.defer, now));
    if (t.flagged) bits.push_back("⚑");
    if (t.estimate) bits.push_back("~" + format_estimate(t.estimate));
    if (const std::string r = repeat_text(t.repeat); !r.empty()) bits.push_back("↻ " + r);
    if (!n.tags.empty()) bits.push_back(tag_words(n.tags));
    std::string s;
    for (const auto& b : bits) s += (s.empty() ? "" : " · ") + b;
    return s;
}

struct Writer {
    const NodeSource& src;
    const std::map<NodeId, std::string>& file_of;
    std::int64_t now;
    std::string out;

    void blank() {
        if (out.size() >= 2 && out.compare(out.size() - 2, 2, "\n\n") == 0) return;
        if (!out.empty()) out += "\n";
    }

    void body_lines(const std::vector<std::string>& ls, const std::string& indent) {
        for (const auto& l : ls) out += (l.empty() ? "" : indent + l) + "\n";
    }

    // A todo, or anything inside a todo: a list item at `depth` (0 = flush).
    void item(const NodeId& id, int depth) {
        const Node* n = src.find(id);
        if (!n) return;
        const std::string ind(static_cast<std::size_t>(depth) * 2, ' ');
        const std::string title = n->title.empty() ? "Untitled" : n->title;
        if (n->task.is_task) {
            out += ind + (n->task.done ? "- [x] " : "- [ ] ") + rewrite_links(title, file_of);
            if (const std::string f = todo_facts(*n, now); !f.empty()) out += " — " + f;
        } else {
            out += ind + "- **" + rewrite_links(title, file_of) + "**";
            if (!n->tags.empty()) out += " " + tag_words(n->tags);
        }
        out += "\n";
        const auto ls = lines_of(rewrite_links(n->body, file_of));
        if (!ls.empty()) body_lines(ls, ind + "  ");
        for (const auto& c : src.children(id)) item(c, depth + 1);
    }

    // A note at heading `level` (2..6) -- or, deeper, a bold bullet.
    void note(const NodeId& id, int level) {
        const Node* n = src.find(id);
        if (!n) return;
        if (n->task.is_task || level > 6) {
            item(id, 0);
            return;
        }
        blank();
        const std::string title = n->title.empty() ? "Untitled" : n->title;
        out += std::string(static_cast<std::size_t>(level), '#') + " " + rewrite_links(title, file_of) + "\n";
        if (!n->tags.empty()) out += "\n" + tag_words(n->tags) + "\n";
        const auto ls = demoted_body(rewrite_links(n->body, file_of), title, level);
        if (!ls.empty()) {
            out += "\n";
            body_lines(ls, "");
        }
        bool list_open = false;
        for (const auto& c : src.children(id)) {
            const Node* k = src.find(c);
            if (!k) continue;
            const bool as_item = k->task.is_task || level + 1 > 6;
            if (as_item) {
                if (!list_open) { blank(); list_open = true; }
                item(c, 0);
            } else {
                list_open = false;
                note(c, level + 1);
            }
        }
    }
};

void front_matter(std::string& out, const NodeSource& src, const Node& n) {
    std::vector<std::pair<std::string, std::string>> kv;
    const Task& t = n.task;
    if (t.is_task) {
        kv.emplace_back("todo", "true");
        if (t.done) kv.emplace_back("done", "true");
        if (t.done && t.finished) kv.emplace_back("completed", iso_when(t.finished));
    }
    if (is_project(src, n.id)) {
        std::string st = lower(project_state_name(t.project));
        kv.emplace_back("project", yaml_str(st));
    }
    if (t.due) kv.emplace_back(t.deadline ? "deadline" : "due", iso_when(t.due));
    if (t.defer) kv.emplace_back("defer", iso_when(t.defer));
    if (t.flagged) kv.emplace_back("flagged", "true");
    if (t.estimate) kv.emplace_back("estimate", yaml_str(format_estimate(t.estimate)));
    if (const std::string r = repeat_text(t.repeat); !r.empty()) kv.emplace_back("repeat", yaml_str(r));
    if (n.packet) kv.emplace_back("packet", "true");
    if (!n.tags.empty()) {
        std::string l = "[";
        for (std::size_t i = 0; i < n.tags.size(); ++i) l += (i ? ", " : "") + yaml_str(n.tags[i]);
        kv.emplace_back("tags", l + "]");
    }
    if (n.created) kv.emplace_back("created", iso_when(n.created));
    kv.emplace_back("jot-id", n.id);
    out += "---\n";
    for (const auto& [k, v] : kv) out += k + ": " + v + "\n";
    out += "---\n";
}

void collect(const NodeSource& src, const NodeId& id, std::vector<NodeId>& all) {
    all.push_back(id);
    for (const auto& c : src.children(id)) collect(src, c, all);
}

std::string unique_in(const fs::path& dir, const std::string& stem, const std::string& ext,
                      std::set<std::string>& taken) {
    for (int k = 1;; ++k) {
        const std::string name = stem + (k == 1 ? "" : " " + std::to_string(k)) + ext;
        std::error_code ec;
        if (!taken.count(lower(name)) && !fs::exists(dir / name, ec)) {
            taken.insert(lower(name));
            return name;
        }
    }
}

}  // namespace

std::string export_file_name(const std::string& title) {
    std::string s;
    for (unsigned char c : title) {
        if (c < 0x20 || std::string("/\\:*?\"<>|").find(static_cast<char>(c)) != std::string::npos) s += '-';
        else s += static_cast<char>(c);
    }
    const auto a = s.find_first_not_of(" .");
    s = a == std::string::npos ? "" : s.substr(a);
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    if (s.size() > 120) {
        std::size_t cut = 120;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;   // never inside a character
        s = s.substr(0, cut);
        while (!s.empty() && s.back() == ' ') s.pop_back();
    }
    if (s.empty()) s = "Untitled";
    return s + ".md";
}

std::vector<NodeId> export_roots(const NodeSource& src, const std::vector<NodeId>& selected) {
    std::set<NodeId> sel(selected.begin(), selected.end());
    std::vector<NodeId> out;
    std::set<NodeId> seen;
    for (const auto& id : selected) {
        if (!src.find(id) || seen.count(id)) continue;
        bool inside = false;
        for (const Node* n = src.find(id); n && !n->parent_id.empty(); n = src.find(n->parent_id))
            if (sel.count(n->parent_id)) { inside = true; break; }
        if (inside) continue;
        seen.insert(id);
        out.push_back(id);
    }
    return out;
}

std::string export_outline(const NodeSource& src, const NodeId& root,
                           const std::map<NodeId, std::string>& file_of, std::int64_t now) {
    const Node* n = src.find(root);
    if (!n) return {};
    Writer w{src, file_of, now, {}};
    front_matter(w.out, src, *n);
    const std::string title = n->title.empty() ? "Untitled" : n->title;
    w.out += "# " + rewrite_links(title, file_of) + "\n";
    const auto ls = demoted_body(rewrite_links(n->body, file_of), title, 1);
    if (!ls.empty()) {
        w.out += "\n";
        w.body_lines(ls, "");
    }
    bool list_open = false;
    for (const auto& c : src.children(root)) {
        const Node* k = src.find(c);
        if (!k) continue;
        if (k->task.is_task) {
            if (!list_open) { w.blank(); list_open = true; }
            w.item(c, 0);
        } else {
            list_open = false;
            w.note(c, 2);
        }
    }
    if (w.out.empty() || w.out.back() != '\n') w.out += "\n";
    return w.out;
}

std::vector<std::string> export_attachment_refs(const std::string& text) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    const std::string key = "attachments/";
    std::size_t i = 0;
    while ((i = text.find(key, i)) != std::string::npos) {
        // Only a link target: "(attachments/" or "(./attachments/".
        const bool target = (i > 0 && text[i - 1] == '(') ||
                            (i > 1 && text.compare(i - 2, 2, "./") == 0 && i > 2 && text[i - 3] == '(');
        const std::size_t s = i + key.size();
        std::size_t e = s;
        while (e < text.size() && text[e] != ')' && text[e] != ' ' && text[e] != '\n' && text[e] != '"') ++e;
        if (target && e > s) {
            const std::string name = text.substr(s, e - s);
            if (name.find("..") == std::string::npos && name.find('/') == std::string::npos &&
                seen.insert(name).second)
                out.push_back(name);
        }
        i = e;
    }
    return out;
}

ExportResult export_to(const NodeSource& src, const std::vector<NodeId>& roots,
                       const std::string& dest, const std::string& attach_dir, std::int64_t now,
                       const std::vector<std::string>& names) {
    ExportResult r;
    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) {
        r.problems.push_back("cannot make " + dest + " (" + ec.message() + ")");
        return r;
    }
    // Names first, for every root, so a link can point at a file written later.
    std::map<NodeId, std::string> file_of;
    std::vector<std::pair<NodeId, std::string>> plan;
    std::set<std::string> taken;
    for (const auto& id : roots) {
        const Node* n = src.find(id);
        if (!n) continue;
        const std::size_t k = static_cast<std::size_t>(&id - roots.data());
        std::string name;
        if (k < names.size() && !names[k].empty()) {
            name = names[k];
            if (name.size() < 3 || lower(name.substr(name.size() - 3)) != ".md") name += ".md";
            taken.insert(lower(name));
        } else {
            std::string base = export_file_name(n->title);
            base.resize(base.size() - 3);   // the ".md"
            name = unique_in(dest, base, ".md", taken);
        }
        plan.emplace_back(id, name);
        std::vector<NodeId> all;
        collect(src, id, all);
        for (const auto& a : all) file_of[a] = name;
    }
    std::set<std::string> wanted;
    for (const auto& [id, name] : plan) {
        const std::string text = export_outline(src, id, file_of, now);
        std::vector<NodeId> all;
        collect(src, id, all);
        r.notes += static_cast<int>(all.size());
        for (const auto& a : all)
            if (const Node* n = src.find(a))
                for (const auto& f : export_attachment_refs(n->body)) wanted.insert(f);
        std::ofstream f(fs::path(dest) / name, std::ios::binary);
        if (!f || !(f << text)) {
            r.problems.push_back("cannot write " + name);
            continue;
        }
        r.files.push_back(name);
    }
    if (!wanted.empty()) {
        const fs::path to = fs::path(dest) / "attachments";
        fs::create_directories(to, ec);
        for (const auto& w : wanted) {
            const fs::path from = fs::path(attach_dir) / w;
            if (!fs::exists(from, ec)) {
                r.problems.push_back("attachments/" + w + " is missing from the jots folder");
                continue;
            }
            fs::copy_file(from, to / w, fs::copy_options::overwrite_existing, ec);
            if (ec) r.problems.push_back("cannot copy attachments/" + w + " (" + ec.message() + ")");
            else ++r.attachments;
        }
    }
    return r;
}

std::string export_folder_name(const std::string& parent, const std::string& stem, std::int64_t now) {
    std::set<std::string> taken;
    const std::string s = export_file_name(stem + " " + iso_when(day_start(now)));
    return unique_in(parent, s.substr(0, s.size() - 3), "", taken);
}

std::string export_free_name(const std::string& parent, const std::string& name) {
    std::set<std::string> taken;
    return unique_in(parent, name, "", taken);
}

}  // namespace jot::core
