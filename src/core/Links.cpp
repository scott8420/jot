#include "core/Links.hpp"
#include "core/Markdown.hpp"

#include <algorithm>

namespace jot::core {
namespace {

const std::vector<LinkRef>& none() {
    static const std::vector<LinkRef> empty;
    return empty;
}

// Collect every `jot:` edge a body declares. Duplicates are kept: a note that
// mentions another one three times has three mentions, and collapsing them here
// would make the count in the drawer disagree with the text on screen.
std::vector<LinkRef> edges_of(const Node& n) {
    std::vector<LinkRef> out;
    const Scan sc = scan(n.body);
    for (const auto& lk : sc.links) {
        const NodeId to = link_node_id(lk.target);
        if (to.empty()) continue;            // http, a path, an attachment
        if (to == n.id) continue;            // a note linking to itself is not a
                                             // relationship, it is a typo
        out.push_back(LinkRef{n.id, to, lk.label, lk.line});
    }
    return out;
}

void walk(const NodeSource& src, const NodeId& parent, std::vector<NodeId>& out) {
    for (const auto& id : src.children(parent)) {
        out.push_back(id);
        walk(src, id, out);
    }
}

}  // namespace

void LinkIndex::forget_out_edges(const NodeId& id) {
    auto it = m_out.find(id);
    if (it == m_out.end()) return;

    // Every edge this note owns has a mirror in the target's incoming list.
    // Pull each one by its `from`, not by value: the label may have been edited
    // in the same keystroke that is driving this update, and matching on a
    // stale label would leave the mirror behind. THAT is the stale backlink.
    for (const auto& e : it->second) {
        auto in = m_in.find(e.to);
        if (in == m_in.end()) continue;
        auto& v = in->second;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const LinkRef& r) { return r.from == id; }),
                v.end());
        if (v.empty()) m_in.erase(in);
    }
    m_out.erase(it);
}

void LinkIndex::rebuild(const NodeSource& src) {
    m_out.clear();
    m_in.clear();

    std::vector<NodeId> all;
    walk(src, NodeId{}, all);
    for (const auto& id : all) {
        const Node* n = src.find(id);
        if (!n) continue;
        auto edges = edges_of(*n);
        if (edges.empty()) continue;
        for (const auto& e : edges) m_in[e.to].push_back(e);
        m_out[id] = std::move(edges);
    }
}

void LinkIndex::update(const NodeSource& src, const NodeId& id) {
    if (id.empty()) return;
    forget_out_edges(id);

    const Node* n = src.find(id);
    if (!n) return;                     // gone: forget_out_edges was the whole job
    auto edges = edges_of(*n);
    if (edges.empty()) return;
    for (const auto& e : edges) m_in[e.to].push_back(e);
    m_out[id] = std::move(edges);
}

void LinkIndex::erase(const NodeId& id) {
    forget_out_edges(id);
    m_in.erase(id);
}

const std::vector<LinkRef>& LinkIndex::outgoing(const NodeId& id) const {
    auto it = m_out.find(id);
    return it == m_out.end() ? none() : it->second;
}

const std::vector<LinkRef>& LinkIndex::incoming(const NodeId& id) const {
    auto it = m_in.find(id);
    return it == m_in.end() ? none() : it->second;
}

std::size_t LinkIndex::edge_count() const {
    std::size_t n = 0;
    for (const auto& [id, v] : m_out) n += v.size();
    return n;
}

}  // namespace jot::core
