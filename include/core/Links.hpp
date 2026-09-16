#pragma once
#include "core/Nodes.hpp"

#include <string>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Links -- who points at whom.
//
// Requirement 8 ("links to related objects in the data structure") has had half
// an answer since s004: a body can contain `[label](jot:<id>)` and it styles.
// The half that makes it a feature is the OTHER direction -- standing on a note
// and seeing what refers to it -- and that cannot be computed from the note in
// front of you, because the answer lives in every other note's body.
//
// So: one pass over every body at load, an in-memory map both ways, and an
// incremental update when a body changes.
//
// WHY THIS IS A CORE FILE AND NOT A DRAWER DETAIL
// ----------------------------------------------
// The failure mode of a backlink index is not that it is slow, it is that it
// goes STALE: note A stops mentioning B, and B's drawer keeps offering a
// backlink to a note that no longer points at it. That is a wrong answer which
// LOOKS like a right one -- it renders, it is clickable, it goes somewhere --
// and nothing on screen would ever reveal it. (CANON: answer the question in
// the layer where a wrong answer is visible.) Here, `update()` dropping a
// vanished link is one selftest check; in the drawer it would be a bug found
// months later by clicking a backlink that should not have existed.
//
// GTK-free, like everything under core/: the index is a pure function of the
// bodies the NodeSource holds, and the selftest can build one, edit a body and
// assert both directions headless.
//
// NOT AN INDEX OF EVERYTHING. It maps `jot:` links only. An `http://` target or
// a relative path to an attachment is a real link and the drawer shows it, but
// it is not a node, so it has no backlink to record. The drawer reads those
// straight off `core::scan(body).links`.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// One edge. `from` and `to` are node ids; `label` is what the link called it,
// which is what the drawer shows -- a backlink row saying "Groceries" is more
// use than one saying "the note that links here".
struct LinkRef {
    NodeId      from;
    NodeId      to;
    std::string label;
    int         line = 0;   // where in `from`'s body -- cheap to carry, and it
                            // is what a future "jump to the mention" needs
};

class LinkIndex {
public:
    // Walk the whole tree from the roots and scan every body. Called once at
    // load, and again after a wholesale reload. Idempotent.
    void rebuild(const NodeSource& src);

    // One note's body changed. Drops everything that note used to point at --
    // in BOTH maps -- then rescans it. This is the whole correctness story:
    // out-edges are owned by exactly one note, so removing that note's edges
    // can never remove somebody else's.
    void update(const NodeSource& src, const NodeId& id);

    // The note is gone. Its out-edges go with it. Links POINTING AT it are left
    // alone on purpose: those bodies still literally say `jot:<id>`, and the
    // drawer showing them as dangling is the truth. Silently deleting them
    // would make a broken link invisible instead of fixing it.
    void erase(const NodeId& id);

    const std::vector<LinkRef>& outgoing(const NodeId& id) const;
    const std::vector<LinkRef>& incoming(const NodeId& id) const;

    std::size_t edge_count() const;

private:
    void forget_out_edges(const NodeId& id);

    std::unordered_map<NodeId, std::vector<LinkRef>> m_out;   // from -> edges
    std::unordered_map<NodeId, std::vector<LinkRef>> m_in;    // to   -> edges
};

}  // namespace jot::core
