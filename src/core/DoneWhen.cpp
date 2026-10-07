#include "core/DoneWhen.hpp"
#include "core/Packet.hpp"

#include <algorithm>

namespace jot::core {

const char* done_when_word(DoneWhen w) {
    switch (w) {
    case DoneWhen::Items: return "items";
    case DoneWhen::Steps: return "steps";
    default:              return "tick";
    }
}

DoneWhen effective_done_when(const Node& n) {
    return n.packet ? DoneWhen::Items : n.task.done_when;
}

DoneState done_state(const NodeSource& src, const NodeId& id) {
    DoneState s;
    const Node* n = src.find(id);
    if (!n) return s;
    s.rule = effective_done_when(*n);
    if (s.rule == DoneWhen::Items) {
        const PacketState p = packet_state(n->body);
        s.in      = p.in;
        s.total   = p.total;
        s.missing = p.missing();
    } else if (s.rule == DoneWhen::Steps) {
        // The DIRECT todos under it (the tree row's "1 of 4" counts the same
        // set). A step dropped or completed as a project is dealt with.
        for (const auto& k : src.children(id)) {
            const Node* c = src.find(k);
            if (!c || !c->task.is_task) continue;
            ++s.total;
            const bool dealt = c->task.done || c->task.project == ProjectState::Completed ||
                               c->task.project == ProjectState::Dropped;
            if (dealt) ++s.in;
            else s.missing.push_back(c->title.empty() ? std::string("(untitled)") : c->title);
        }
    }
    return s;
}

std::string done_count(const DoneState& s) {
    if (!s.counted()) return {};
    const std::string of = std::to_string(s.in) + " of " + std::to_string(s.total);
    return s.rule == DoneWhen::Items ? of + " in" : of + " steps";
}

const std::vector<std::string>& done_when_choices() {
    static const std::vector<std::string> v{"Just the tick", "Every item is in", "Every step is done"};
    return v;
}

std::string done_when_hint(DoneWhen w, bool packet) {
    if (packet) return "A packet is done when every item is in.";
    switch (w) {
    case DoneWhen::Items:
        return "Each checkbox line in its text is a thing it needs: in when ticked or a file sits on the line.";
    case DoneWhen::Steps:
        return "Done when every todo directly under it is done.";
    default:
        return "Done is whatever the tick says. Choose a rule and jot asks before a tick that comes too soon.";
    }
}

bool finishes(const Task& before, const Task& after) {
    if (after.is_task && after.done && !before.done) return true;
    return after.project == ProjectState::Completed && before.project != ProjectState::Completed;
}

namespace {
std::string quoted_title(const Node* n) {
    return "“" + (n && !n->title.empty() ? n->title : std::string("Untitled")) + "”";
}
std::string missing_text(const std::vector<std::string>& m) {
    std::string out;
    const std::size_t shown = std::min<std::size_t>(m.size(), 3);
    for (std::size_t i = 0; i < shown; ++i) {
        if (i) out += ", ";
        out += m[i];
    }
    if (m.size() > shown) out += " and " + std::to_string(m.size() - shown) + " more";
    return out;
}
}  // namespace

DoneAsk done_ask(const NodeSource& src, const std::vector<NodeId>& ids, bool completing) {
    DoneAsk a;
    a.button = completing ? "Complete Anyway" : "Tick Anyway";
    std::vector<std::pair<const Node*, DoneState>> open;
    for (const auto& id : ids) {
        DoneState s = done_state(src, id);
        if (!s.met()) open.emplace_back(src.find(id), std::move(s));
    }
    if (open.empty()) return a;
    if (open.size() == 1) {
        const auto& [n, s] = open.front();
        a.message = quoted_title(n) + " is " + done_count(s);
        a.detail  = (s.rule == DoneWhen::Items ? "Missing: " : "Not done yet: ") +
                    missing_text(s.missing) + ".";
        return a;
    }
    a.message = std::to_string(open.size()) + " aren’t finished yet";
    for (const auto& [n, s] : open) {
        if (!a.detail.empty()) a.detail += "\n";
        a.detail += quoted_title(n) + " — " + done_count(s);
    }
    return a;
}

}  // namespace jot::core
