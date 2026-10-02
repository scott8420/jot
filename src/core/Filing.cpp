#include "core/Filing.hpp"

#include <algorithm>
#include <cctype>

// core/Filing.cpp -- where a note can go. See the header for the rules.

namespace jot::core {
namespace {

constexpr const char* kSep = " › ";   // " › "

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string shown_title(const Node& n) { return n.title.empty() ? std::string("Untitled") : n.title; }

}  // namespace

std::string node_path(const NodeSource& src, const NodeId& id) {
    std::vector<std::string> up;
    const Node* n = src.find(id);
    // Bounded by count() so a corrupt parent cycle cannot hang the picker.
    for (std::size_t guard = 0; n && !n->parent_id.empty() && guard <= src.count(); ++guard) {
        n = src.find(n->parent_id);
        if (n) up.push_back(shown_title(*n));
    }
    std::string out;
    for (auto it = up.rbegin(); it != up.rend(); ++it) {
        if (!out.empty()) out += kSep;
        out += *it;
    }
    return out;
}

std::vector<MoveTarget> move_targets(const NodeSource& src, const NodeId& moving,
                                     const std::string& query) {
    std::vector<MoveTarget> all;
    const Node* m = src.find(moving);
    if (!m || m->protect) return all;

    if (!m->parent_id.empty()) all.push_back({NodeId{}, kTopLevel, ""});

    // Depth-first, tree order, carrying the path down so it is built once per
    // node rather than walked upward per node. The moving note's own subtree
    // is skipped whole: nothing in it is a legal target (can_move would say
    // so one node at a time; skipping says it once).
    struct Frame { NodeId id; std::string path; };
    std::vector<Frame> stack;
    {
        auto roots = src.children("");
        for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back({*it, ""});
    }
    while (!stack.empty()) {
        Frame f = std::move(stack.back());
        stack.pop_back();
        if (f.id == moving) continue;
        const Node* n = src.find(f.id);
        if (!n) continue;
        if (f.id != m->parent_id) all.push_back({f.id, shown_title(*n), f.path});
        const std::string below = f.path.empty() ? shown_title(*n)
                                                 : f.path + kSep + shown_title(*n);
        auto kids = src.children(f.id);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back({*it, below});
    }

    const std::string q = lower(trim(query));
    if (q.empty()) return all;

    std::vector<MoveTarget> starts, contains, inpath;
    for (auto& t : all) {
        const std::string tl = lower(t.title);
        if (tl.rfind(q, 0) == 0)                       starts.push_back(std::move(t));
        else if (tl.find(q) != std::string::npos)      contains.push_back(std::move(t));
        else if (lower(t.path).find(q) != std::string::npos) inpath.push_back(std::move(t));
    }
    starts.insert(starts.end(), std::make_move_iterator(contains.begin()),
                  std::make_move_iterator(contains.end()));
    starts.insert(starts.end(), std::make_move_iterator(inpath.begin()),
                  std::make_move_iterator(inpath.end()));
    return starts;
}

void remember_target(std::vector<NodeId>& recent, const NodeId& target, std::size_t cap) {
    if (target.empty() || cap == 0) return;
    recent.erase(std::remove(recent.begin(), recent.end(), target), recent.end());
    recent.insert(recent.begin(), target);
    if (recent.size() > cap) recent.resize(cap);
}

std::vector<MoveTarget> recent_targets(const NodeSource& src, const NodeId& moving,
                                       const std::vector<NodeId>& recent) {
    std::vector<MoveTarget> out;
    const Node* m = src.find(moving);
    if (!m) return out;
    for (const auto& id : recent) {
        const Node* n = src.find(id);
        if (!n || id == m->parent_id || !can_move(src, moving, id)) continue;
        out.push_back({id, shown_title(*n), node_path(src, id)});
    }
    return out;
}

}  // namespace jot::core
