#include "core/Packet.hpp"
#include "core/Enclosures.hpp"
#include "core/Markdown.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace jot::core {
namespace {

// Collapse runs of spaces and trim -- what is left when a link is lifted out
// of the middle of a line.
std::string tidy(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t') { space = !out.empty(); continue; }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

}  // namespace

std::vector<std::string> PacketState::missing() const {
    std::vector<std::string> out;
    for (const auto& it : items)
        if (!it.in()) out.push_back(it.label);
    return out;
}

PacketState packet_state(const std::string& body) {
    PacketState st;
    const Scan sc = scan(body);
    for (std::size_t i = 0; i < sc.lines.size(); ++i) {
        const Line& ln = sc.lines[i];
        if (ln.block != Block::Task) continue;

        PacketItem it;
        it.line   = static_cast<int>(i);
        it.ticked = ln.checked;

        // The words, with every link on the line lifted out: a dropped file
        // is the ANSWER to the item, not part of its name.
        std::string words;
        int at = ln.content;
        for (const Link& lk : sc.links) {
            if (lk.line != it.line) continue;
            if (lk.begin > at) words += body.substr(at, lk.begin - at);
            at = std::max(at, lk.end);
            const std::string name = attachment_name(lk.target);
            if (!name.empty()) it.files.push_back(name);
            else if (!linked_path(lk.target).empty()) it.files.push_back(linked_key(lk.target));
        }
        if (ln.end > at) words += body.substr(at, ln.end - at);
        it.label = tidy(words);
        if (it.label.empty()) it.label = it.files.empty() ? std::string("(unnamed)") : it.files[0];

        ++st.total;
        if (it.in()) ++st.in;
        st.items.push_back(std::move(it));
    }
    return st;
}

std::string packet_line(const PacketState& s) {
    if (s.total == 0) return "No items yet — add a checkbox line for each thing it needs.";
    if (s.complete()) return "All " + std::to_string(s.total) + " in";
    std::string out = std::to_string(s.in) + " of " + std::to_string(s.total) + " in";
    const auto miss = s.missing();
    out += "  ·  missing: ";
    for (std::size_t i = 0; i < miss.size() && i < 3; ++i) out += (i ? ", " : "") + miss[i];
    if (miss.size() > 3) out += " and " + std::to_string(miss.size() - 3) + " more";
    return out;
}

std::string packet_count(const PacketState& s) {
    if (s.total == 0 || s.complete()) return packet_line(s);
    return std::to_string(s.in) + " of " + std::to_string(s.total) + " in  \u00b7  " +
           std::to_string(s.total - s.in) + " missing";
}

// ── s053 ───────────────────────────────────────────────────────────────────
namespace {

bool digit(char c) { return c >= '0' && c <= '9'; }

std::int64_t plus_year(std::int64_t t) {
    if (t == 0) return 0;
    std::tm tm{};
    const std::time_t tt = static_cast<std::time_t>(t);
    localtime_r(&tt, &tm);
    tm.tm_year += 1;
    tm.tm_isdst = -1;
    if (tm.tm_mon == 1 && tm.tm_mday == 29) tm.tm_mday = 28;   // Feb 29 -> Feb 28
    return static_cast<std::int64_t>(std::mktime(&tm));
}

constexpr const char* kLastTime = "Last time: ";

}  // namespace

std::string next_title(const std::string& title) {
    for (std::size_t i = title.size(); i >= 4; --i) {
        const std::size_t b = i - 4;
        if (!(digit(title[b]) && digit(title[b + 1]) && digit(title[b + 2]) && digit(title[b + 3]))) continue;
        if ((b > 0 && digit(title[b - 1])) || (i < title.size() && digit(title[i]))) continue;
        const int y = std::stoi(title.substr(b, 4));
        if (y < 1900 || y > 2099) continue;
        return title.substr(0, b) + std::to_string(y + 1) + title.substr(i);
    }
    return (title.empty() ? std::string("Untitled") : title) + " (next)";
}

std::string fresh_body(const std::string& body) {
    const Scan sc = scan(body);
    std::string out;
    for (std::size_t i = 0; i < sc.lines.size(); ++i) {
        const Line& ln = sc.lines[i];
        std::string text = body.substr(static_cast<std::size_t>(ln.begin),
                                       static_cast<std::size_t>(ln.end - ln.begin));
        if (text.rfind(kLastTime, 0) == 0) continue;            // an earlier round's footer
        if (ln.block == Block::Task) {
            // Rebuild from the end so earlier offsets stay good.
            std::vector<std::pair<int, int>> cut;
            for (const Link& lk : sc.links) {
                if (lk.line != static_cast<int>(i)) continue;
                if (!attachment_name(lk.target).empty() || !linked_path(lk.target).empty())
                    cut.push_back({lk.begin - ln.begin, lk.end - ln.begin});
            }
            for (auto it = cut.rbegin(); it != cut.rend(); ++it) {
                int from = it->first;
                if (from > 0 && text[static_cast<std::size_t>(from) - 1] == '!') --from;   // an image's bang
                text.erase(static_cast<std::size_t>(from), static_cast<std::size_t>(it->second - from));
            }
            if (ln.checked && ln.box_begin >= 0) text[static_cast<std::size_t>(ln.box_begin - ln.begin) + 1] = ' ';
            // Fold the gaps the cuts left, in the words only (not the indent).
            const std::size_t words = static_cast<std::size_t>(std::max(0, ln.content - ln.begin));
            std::string tidy = text.substr(0, std::min(words, text.size()));
            for (std::size_t k = tidy.size(); k < text.size(); ++k)
                if (!(text[k] == ' ' && !tidy.empty() && tidy.back() == ' ')) tidy += text[k];
            text = std::move(tidy);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
        }
        out += text;
        out += '\n';
    }
    while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
    return out;
}

NodeId packet_again(NodeSource& src, const NodeId& id) {
    const Node* old = src.find(id);
    if (!old || !old->packet) return {};
    const Node was = *old;   // copy: the writes below may move storage

    const NodeId nid = src.create(was.parent_id, next_title(was.title));
    if (nid.empty()) return {};
    src.move(nid, was.parent_id, sibling_index(src, id) + 1);

    std::string body = fresh_body(was.body);
    if (!body.empty() && body.back() != '\n') body += '\n';
    body += "\n" + std::string(kLastTime) + "[" + (was.title.empty() ? std::string("Untitled") : was.title) +
            "](" + kJotScheme + id + ")";
    if (was.sent) {
        std::tm tm{};
        const std::time_t t = static_cast<std::time_t>(was.sent);
        localtime_r(&t, &tm);
        char buf[32];
        std::strftime(buf, sizeof buf, "%e %b %Y", &tm);
        std::string d = buf;
        while (!d.empty() && d.front() == ' ') d.erase(d.begin());
        body += " — sent " + d;
    }
    body += ".\n";
    src.set_body(nid, body);
    src.set_packet(nid, true);
    if (was.nudge) src.set_nudge(nid, was.nudge);
    if (!was.tags.empty()) src.set_tags(nid, was.tags);   // s062
    if (was.task.is_task) {
        Task t = was.task;
        t.done = false;
        t.finished = 0;
        t.flagged = false;
        t.due = plus_year(t.due);
        t.defer = plus_year(t.defer);
        src.set_task(nid, t);
    } else if (was.task.mark != ProjectMark::Auto || was.task.status != Status::None) {
        Task t;
        t.mark = was.task.mark;
        t.status = was.task.status;
        src.set_task(nid, t);
    }
    return nid;
}

}  // namespace jot::core
