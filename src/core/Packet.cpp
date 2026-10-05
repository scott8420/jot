#include "core/Packet.hpp"
#include "core/Enclosures.hpp"
#include "core/Markdown.hpp"

#include <algorithm>

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

}  // namespace jot::core
