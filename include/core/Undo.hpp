#pragma once
#include "core/Nodes.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Undo (s045) -- undo and redo for what the NAVIGATOR does: new, delete,
// move, indent, rename, tick, flag, make-a-todo. (The note body has its own
// undo, the text view's; this is for the model verbs that had none -- s029:
// "an app-level undo for model verbs is its own milestone".) Pure: no GTK.
//
// ── a step is two pictures, not an inverse operation ───────────────────────
// Each step records how the nodes it touched looked BEFORE and AFTER: every
// field, the parent, the place among the siblings, or "absent". Undo makes
// the model look like BEFORE; redo makes it look like AFTER. There is no
// per-verb inverse to get wrong -- delete's inverse is "these twelve nodes are
// present again, here", which is the same code as move's and rename's. A verb
// added later is undoable the day it is wrapped in Journal::run.
//
// ── deleted means the whole subtree, text included ─────────────────────────
// A delete is recorded with its subtree, bodies and all, and comes back under
// the SAME ids -- so links to it, its attachments and its place in Recent are
// whole again. NodeSource::restore is the door for that.
//
// ── what it does not cover ─────────────────────────────────────────────────
// A repeating todo ticked and then undone goes back to its old dates, but the
// Logbook keeps the occurrence it recorded (the history is append-only).
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

struct NodeSnap {
    NodeId id;
    bool   present = false;
    Node   node;           // valid when present; node.parent_id is the parent
    int    index = -1;     // place among the parent's children
    int    depth = 0;      // for ordering the restore: parents first
};
using Snapshot = std::vector<NodeSnap>;

// How `ids` look now (with every descendant, when `subtrees`).
Snapshot snapshot(const NodeSource& src, const std::vector<NodeId>& ids, bool subtrees);

// Make the source match the picture for the nodes it names. With `other` (the
// step's other side), only what DIFFERS between the two is written -- so
// undoing a rename does not also undo text typed into that note since. A node
// absent on one side is written whole. Returns false if any part was refused
// (a protected node, say); the rest is still applied.
bool apply(NodeSource& src, const Snapshot& want, const Snapshot* other = nullptr);

struct UndoStep {
    std::string label;     // "Delete “Taxes”", "Move", "Indent" ...
    Snapshot    before;
    Snapshot    after;
    NodeId      focus;     // the node to select after undo / redo
};

class Journal {
public:
    // Run `op` as one undoable step. `ids` are the nodes it will touch;
    // `subtrees` records them with everything under them (delete, move).
    // `op` returns any nodes it CREATED, which are recorded as absent before.
    // A step that changed nothing is not kept.
    using Op = std::function<std::vector<NodeId>()>;
    bool run(NodeSource& src, const std::string& label, const std::vector<NodeId>& ids,
             bool subtrees, const Op& op);

    bool can_undo() const { return m_at > 0; }
    bool can_redo() const { return m_at < m_steps.size(); }
    std::string undo_label() const;   // "" when nothing
    // The node the last undo / redo is about -- select it so the user SEES
    // what came back. Empty if none.
    NodeId last_focus() const { return m_last_focus; }
    std::string redo_label() const;
    // Each returns the step's label ("" if there was nothing to do).
    std::string undo(NodeSource& src);
    std::string redo(NodeSource& src);

    void clear();                      // a different jots folder: nothing carries over
    std::size_t size() const { return m_steps.size(); }

    static constexpr std::size_t kMax = 200;

    // While true, run() just runs the op (used while undo/redo apply, and by
    // callers composing several verbs into one step).
    bool quiet() const { return m_quiet; }

private:
    std::vector<UndoStep> m_steps;
    std::size_t           m_at = 0;     // steps [0, m_at) are done; the rest are redoable
    bool                  m_quiet = false;
    NodeId                m_last_focus;
    const NodeSource*     m_seen = nullptr;   // a different store: forget every step
    void guard(const NodeSource& src) { if (&src != m_seen) { clear(); m_seen = &src; } }
};

}  // namespace jot::core
