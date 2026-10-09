#include "core/Graph.hpp"

#include "core/Errands.hpp"
#include "core/Links.hpp"
#include "core/Review.hpp"
#include "core/Tags.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <unordered_map>

// core/Graph.cpp -- see the header.

namespace jot::core {

int Graph::index_of(const NodeId& id) const {
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].id == id) return static_cast<int>(i);
    return -1;
}

namespace {

void walk(const NodeSource& src, const NodeId& parent, std::vector<const Node*>& out) {
    for (const auto& c : src.children(parent)) {
        if (const Node* n = src.find(c)) {
            out.push_back(n);
            walk(src, c, out);
        }
    }
}

// FNV-1a: a stable number from an id, for a first place.
std::uint64_t hash_of(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

}  // namespace

std::string graph_group_of(const NodeSource& src, const Node& n, GraphGroup group) {
    switch (group) {
        case GraphGroup::None: return {};
        case GraphGroup::Tag:
            for (const auto& t : node_tags(n))
                if (t.rfind("at/", 0) != 0 && t != "at") return "#" + t;
            return {};
        case GraphGroup::Place: {
            const auto ps = node_places(n);
            return ps.empty() ? std::string{} : place_title(ps.front());
        }
        case GraphGroup::Project: {
            // The OUTERMOST project it is or is in: Utilities inside Move house
            // is Move house's clump, not a clump of its own.
            std::string out;
            for (const Node* p = &n; p; p = p->parent_id.empty() ? nullptr : src.find(p->parent_id))
                if (is_project(src, p->id)) out = p->title.empty() ? "Untitled" : p->title;
            return out;
        }
    }
    return {};
}

Graph graph_build(const NodeSource& src, const GraphShow& show, GraphGroup group, std::int64_t now) {
    Graph g;
    std::vector<const Node*> all;
    walk(src, "", all);
    std::unordered_map<NodeId, int> at;
    for (const Node* n : all) {
        const bool todo = n->task.is_task;
        const bool proj = is_project(src, n->id);
        if (!show.notes && !todo && !proj) continue;
        if (!show.done && todo && n->task.done) continue;
        GraphNode gn;
        gn.id = n->id;
        gn.title = n->title.empty() ? "Untitled" : n->title;
        gn.todo = todo;
        gn.project = proj;
        if (todo) gn.state = row_look(src, *n, now).state;
        gn.group = graph_group_of(src, *n, group);
        if (!gn.group.empty() && std::find(g.groups.begin(), g.groups.end(), gn.group) == g.groups.end())
            g.groups.push_back(gn.group);
        at[n->id] = static_cast<int>(g.nodes.size());
        g.nodes.push_back(std::move(gn));
    }
    std::set<std::tuple<int, int, int>> seen;
    auto add = [&](const NodeId& x, const NodeId& y, GraphEdgeKind k) {
        const auto ix = at.find(x), iy = at.find(y);
        if (ix == at.end() || iy == at.end() || ix->second == iy->second) return;
        int a = ix->second, b = iy->second;
        if (a > b) std::swap(a, b);
        if (!seen.insert({a, b, static_cast<int>(k)}).second) return;
        g.edges.push_back({a, b, k});
    };
    if (show.tree)
        for (const Node* n : all)
            if (!n->parent_id.empty()) add(n->parent_id, n->id, GraphEdgeKind::Tree);
    if (show.links) {
        LinkIndex li;
        li.rebuild(src);
        for (const Node* n : all)
            for (const auto& r : li.outgoing(n->id)) add(r.from, r.to, GraphEdgeKind::Link);
    }
    if (show.tags) {
        std::map<std::string, std::vector<NodeId>> by;
        for (const Node* n : all)
            if (at.count(n->id))
                for (const auto& t : node_tags(*n)) by[t].push_back(n->id);
        for (const auto& [t, ids] : by) {
            if (ids.size() < 2 || static_cast<int>(ids.size()) > kTagFan) continue;
            for (std::size_t i = 0; i < ids.size(); ++i)
                for (std::size_t j = i + 1; j < ids.size(); ++j) add(ids[i], ids[j], GraphEdgeKind::Tag);
        }
    }
    for (const auto& e : g.edges) {
        ++g.nodes[static_cast<std::size_t>(e.a)].degree;
        ++g.nodes[static_cast<std::size_t>(e.b)].degree;
    }
    return g;
}

std::set<int> graph_around(const Graph& g, int center, int depth) {
    std::set<int> out;
    if (center < 0 || center >= static_cast<int>(g.nodes.size())) return out;
    std::vector<std::vector<int>> adj(g.nodes.size());
    for (const auto& e : g.edges) {
        adj[static_cast<std::size_t>(e.a)].push_back(e.b);
        adj[static_cast<std::size_t>(e.b)].push_back(e.a);
    }
    std::vector<int> ring{center};
    out.insert(center);
    for (int d = 0; d < depth; ++d) {
        std::vector<int> next;
        for (int i : ring)
            for (int j : adj[static_cast<std::size_t>(i)])
                if (out.insert(j).second) next.push_back(j);
        ring = std::move(next);
    }
    return out;
}

const char* graph_group_word(GraphGroup g) {
    switch (g) {
        case GraphGroup::None:    return "none";
        case GraphGroup::Tag:     return "tag";
        case GraphGroup::Place:   return "place";
        case GraphGroup::Project: return "project";
    }
    return "none";
}

GraphGroup graph_group_parse(const std::string& s) {
    if (s == "tag") return GraphGroup::Tag;
    if (s == "place") return GraphGroup::Place;
    if (s == "project") return GraphGroup::Project;
    return GraphGroup::None;
}

std::string graph_show_words(const GraphShow& s) {
    std::string out;
    auto add = [&](bool on, const char* w) { if (on) out += (out.empty() ? "" : ",") + std::string(w); };
    add(s.links, "links");
    add(s.tree, "tree");
    add(s.tags, "tags");
    add(s.done, "done");
    add(s.notes, "notes");
    return out.empty() ? "none" : out;
}

GraphShow graph_show_parse(const std::string& s) {
    if (s.empty()) return {};
    GraphShow out{false, false, false, false, false};
    bool any = false;
    std::stringstream ss(s);
    std::string w;
    while (std::getline(ss, w, ',')) {
        if (w == "links") out.links = true;
        else if (w == "tree") out.tree = true;
        else if (w == "tags") out.tags = true;
        else if (w == "done") out.done = true;
        else if (w == "notes") out.notes = true;
        else if (w == "none") {}
        else continue;
        any = true;
    }
    return any ? out : GraphShow{};
}

double graph_radius(const GraphNode& n) {
    return 5.0 + 2.2 * std::sqrt(static_cast<double>(n.degree)) + (n.project ? 2.0 : 0.0);
}

// ── layout ──────────────────────────────────────────────────────────────────

namespace {

// The middle of a group's clump: groups sit on a ring, no group in the middle.
void group_centres(const Graph& g, std::map<std::string, std::pair<double, double>>& c) {
    const double n = static_cast<double>(g.groups.size());
    const double r = n <= 1 ? 0.0 : 200.0 + 70.0 * n;
    for (std::size_t i = 0; i < g.groups.size(); ++i) {
        const double a = 2.0 * M_PI * static_cast<double>(i) / std::max(1.0, n) - M_PI / 2;
        c[g.groups[i]] = {r * std::cos(a), r * std::sin(a)};
    }
}

}  // namespace

std::vector<GraphPos> graph_seed(const Graph& g, const Graph* old_graph, const std::vector<GraphPos>* old) {
    std::map<std::string, std::pair<double, double>> centres;
    group_centres(g, centres);
    std::vector<GraphPos> out(g.nodes.size());
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const auto& n = g.nodes[i];
        if (old_graph && old) {
            const int k = old_graph->index_of(n.id);
            if (k >= 0 && static_cast<std::size_t>(k) < old->size()) {
                out[i] = (*old)[static_cast<std::size_t>(k)];
                out[i].vx = out[i].vy = 0;
                continue;
            }
        }
        const std::uint64_t h = hash_of(n.id);
        const double a = static_cast<double>(h % 3600) / 3600.0 * 2.0 * M_PI;
        const double r = 30.0 + static_cast<double>((h >> 12) % 1000) / 1000.0 *
                                    (40.0 + 12.0 * std::sqrt(static_cast<double>(g.nodes.size())));
        double cx = 0, cy = 0;
        if (auto it = centres.find(n.group); it != centres.end()) { cx = it->second.first; cy = it->second.second; }
        out[i].x = cx + r * std::cos(a);
        out[i].y = cy + r * std::sin(a);
    }
    return out;
}

double graph_step(const Graph& g, std::vector<GraphPos>& pos, const std::set<int>& focus) {
    const std::size_t n = g.nodes.size();
    if (pos.size() != n || n == 0) return 0;
    auto moves = [&](std::size_t i) { return focus.empty() || focus.count(static_cast<int>(i)); };
    std::vector<double> fx(n, 0.0), fy(n, 0.0);
    // Push apart.
    for (std::size_t i = 0; i < n; ++i) {
        if (!moves(i)) continue;
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!moves(j)) continue;
            double dx = pos[i].x - pos[j].x, dy = pos[i].y - pos[j].y;
            double d2 = dx * dx + dy * dy;
            if (d2 < 0.01) { dx = 0.1 * static_cast<double>((i * 7 + j) % 5 + 1); dy = 0.1; d2 = dx * dx + dy * dy; }
            const double minr = graph_radius(g.nodes[i]) + graph_radius(g.nodes[j]) + 14.0;
            const double f = 3400.0 / std::max(d2, minr * minr * 0.25);
            const double d = std::sqrt(d2);
            fx[i] += f * dx / d; fy[i] += f * dy / d;
            fx[j] -= f * dx / d; fy[j] -= f * dy / d;
        }
    }
    // Lines pull together.
    for (const auto& e : g.edges) {
        const auto a = static_cast<std::size_t>(e.a), b = static_cast<std::size_t>(e.b);
        if (!moves(a) && !moves(b)) continue;
        const double dx = pos[b].x - pos[a].x, dy = pos[b].y - pos[a].y;
        const double d = std::max(0.1, std::sqrt(dx * dx + dy * dy));
        const double rest = e.kind == GraphEdgeKind::Tag ? 110.0 : 70.0;
        const double k = e.kind == GraphEdgeKind::Tag ? 0.01 : 0.03;
        const double f = k * (d - rest);
        fx[a] += f * dx / d; fy[a] += f * dy / d;
        fx[b] -= f * dx / d; fy[b] -= f * dy / d;
    }
    // A gentle pull to the middle -- or to the group's clump.
    std::map<std::string, std::pair<double, double>> centres;
    group_centres(g, centres);
    double cx = 0, cy = 0;
    if (!focus.empty()) {
        for (int i : focus) { cx += pos[static_cast<std::size_t>(i)].x; cy += pos[static_cast<std::size_t>(i)].y; }
        cx /= static_cast<double>(focus.size());
        cy /= static_cast<double>(focus.size());
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!moves(i)) continue;
        double tx = cx, ty = cy, k = 0.004;
        if (auto it = centres.find(g.nodes[i].group); focus.empty() && it != centres.end()) {
            tx = it->second.first; ty = it->second.second; k = 0.007;
        }
        fx[i] += k * (tx - pos[i].x);
        fy[i] += k * (ty - pos[i].y);
    }
    double moved = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!moves(i) || pos[i].pinned) { pos[i].vx = pos[i].vy = 0; continue; }
        pos[i].vx = (pos[i].vx + fx[i]) * 0.82;
        pos[i].vy = (pos[i].vy + fy[i]) * 0.82;
        const double sp = std::sqrt(pos[i].vx * pos[i].vx + pos[i].vy * pos[i].vy);
        if (sp > 25.0) { pos[i].vx *= 25.0 / sp; pos[i].vy *= 25.0 / sp; }
        pos[i].x += pos[i].vx;
        pos[i].y += pos[i].vy;
        moved += std::min(sp, 25.0);
    }
    return moved;
}

}  // namespace jot::core
