#pragma once
// core::Graph -- the graph view (s071f).
//
// Scott, Oct 8: jot "could replace Iotas, Obsidian and Marker ... one thing
// jot doesn't do is show a cluster view like obsidian" -- and before that
// (Oct 6) "a graphical view of clustered bubbles showing relationships among
// tasks and notes". Obsidian's graph, the jot way:
//
//   * every note a bubble, sized by how many lines meet it;
//   * lines from `jot:` links, from the tree (parent -> child), and -- off by
//     default -- between notes that share a tag;
//   * todos in their state's colour, as the cards wear it;
//   * the whole folder at first; "Center Here" on a bubble gathers the graph
//     to it and what is within two lines of it (Scott: "right-click a context
//     on a bubble and say center on here after the default build");
//   * Group (Tag / Place / Project) pulls each group into its own clump with
//     a halo and a name -- the clustering Obsidian does not do.
//
// The layout is a small force simulation -- bubbles push apart, lines pull
// together, a gentle pull to the middle (or to the group's centre) -- and it
// is here, GTK-free and deterministic (a bubble's first place comes from its
// id), so the same folder settles the same way and the selftest can watch it.
#include "core/Nodes.hpp"
#include "core/RowLook.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace jot::core {

enum class GraphEdgeKind { Link, Tree, Tag };

struct GraphNode {
    NodeId      id;
    std::string title;
    bool        todo = false;
    RowState    state = RowState::Available;   // meaningful for a todo
    bool        project = false;
    int         degree = 0;      // lines meeting it, of the kinds shown
    std::string group;           // "" = in no group (Group off, or none fits)
};

struct GraphEdge {
    int           a = 0, b = 0;  // indices into Graph::nodes, a < b
    GraphEdgeKind kind = GraphEdgeKind::Link;
};

struct GraphShow {
    bool links = true;           // `jot:` links
    bool tree  = true;           // parent -> child
    bool tags  = false;          // shared tags (busy: off at first)
    bool done  = true;           // finished todos
    bool notes = true;           // plain notes (not todos)
};

enum class GraphGroup { None, Tag, Place, Project };

struct Graph {
    std::vector<GraphNode>   nodes;
    std::vector<GraphEdge>   edges;
    std::vector<std::string> groups;   // the group names in use, in first-seen order
    int index_of(const NodeId& id) const;   // -1 if not drawn
};

// The folder as bubbles and lines. Edges are unique per pair and kind; a
// tag line joins notes that share a tag directly only when the tag has at
// most `kTagFan` notes (more is one hairball, drawn better by Group).
inline constexpr int kTagFan = 12;
Graph graph_build(const NodeSource& src, const GraphShow& show, GraphGroup group, std::int64_t now);

// The bubbles within `depth` lines of `center` (it included). Empty if the
// centre is not drawn.
std::set<int> graph_around(const Graph& g, int center, int depth = 2);

// The group a node falls in -- its first tag that is not a place, its first
// place (#at/...), or the outermost project it is or is in. "" = none.
std::string graph_group_of(const NodeSource& src, const Node& n, GraphGroup group);

const char* graph_group_word(GraphGroup g);           // "none", "tag", "place", "project"
GraphGroup  graph_group_parse(const std::string& s);  // junk -> None
std::string graph_show_words(const GraphShow& s);     // "links,tree" ...
GraphShow   graph_show_parse(const std::string& s);   // "" or junk -> the default

// ── the layout ──────────────────────────────────────────────────────────────
struct GraphPos {
    double x = 0, y = 0, vx = 0, vy = 0;
    bool   pinned = false;
};

// A first place for each bubble, from its id (so the same folder starts the
// same way), keeping the place of any bubble already in `old` by id.
std::vector<GraphPos> graph_seed(const Graph& g, const Graph* old_graph = nullptr,
                                 const std::vector<GraphPos>* old = nullptr);

// One step of the simulation. `focus` non-empty: only those bubbles move and
// pull (the rest are left where they are, faded by the view). Returns how
// much moved -- the view stops ticking when it is small.
double graph_step(const Graph& g, std::vector<GraphPos>& pos, const std::set<int>& focus = {});

// The bubble's radius on screen at zoom 1: grows with its lines.
double graph_radius(const GraphNode& n);

}  // namespace jot::core
