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

// s046. Where the selection goes when `id` is deleted: the next sibling, else
// the one before, else the parent (empty for a lone root). Ask BEFORE the
// delete -- afterwards there is no "beside it".
NodeId neighbour_after_delete(const NodeSource& src, const NodeId& id);

struct UndoStep {
    std::string label;     // "Delete “Taxes”", "Move", "Indent" ...
    Snapshot    before;
    Snapshot    after;
    NodeId      focus;     // the node to select after undo / redo
    std::string merge;     // s046b: a later step with the same key, soon after, folds in
    std::vector<NodeId> selection;   // s049: what was selected when it was made
    double      when = 0;  // s046b: the journal clock at commit (seconds)
};

// s046b. What a task-record change is called on the Undo menu: the first
// field that differs ("Tick", "Due date", "Estimate" ...). "Edit todo" if
// none of the named ones did.
std::string task_label(const Task& before, const Task& after);

class Journal {
public:
    // Run `op` as one undoable step. `ids` are the nodes it will touch;
    // `subtrees` records them with everything under them (delete, move).
    // `op` returns any nodes it CREATED, which are recorded as absent before.
    // A step that changed nothing is not kept.
    using Op = std::function<std::vector<NodeId>()>;
    bool run(NodeSource& src, const std::string& label, const std::vector<NodeId>& ids,
             bool subtrees, const Op& op);

    // ── s046b: a GESTURE -- any number of writes, one step ──────────────────
    // (Borrowed from Cairn's undo stone.) begin() opens it; touch() records
    // the BEFORE picture of a node the first time it is about to be written
    // (later touches of the same node keep the first picture); made() names a
    // node the gesture created; commit() takes the AFTER pictures and keeps
    // the step if anything changed. Gestures nest: only the outermost
    // begin's label counts and only its commit closes the step -- so a verb
    // made of verbs (Import: create + body, per file) is ONE Ctrl+Z.
    //
    // `merge`: a non-empty key folds this step into the previous one when that
    // one has the same key, was committed less than kMergeSeconds ago, and is
    // the newest (nothing was undone since) -- so typing a name in Note
    // details is one step per field, not one per keystroke.
    void begin(const NodeSource& src, const std::string& label, const std::string& merge = {});
    void touch(const NodeSource& src, const std::vector<NodeId>& ids, bool subtrees);
    void made(const NodeId& id);
    bool commit(const NodeSource& src);   // true if a step was kept (or merged)
    bool in_gesture() const { return m_depth > 0; }
    static constexpr double kMergeSeconds = 1.5;
    // Seconds on any monotonic scale; the selftest stands it still.
    void set_clock(std::function<double()> fn) { m_clock = std::move(fn); }
    // Called after every kept step, undo, redo and clear -- the Shell greys
    // its menu items from here, whichever pane made the step.
    void on_change(std::function<void()> fn) { m_changed = std::move(fn); }

    bool can_undo() const { return m_at > 0; }
    bool can_redo() const { return m_at < m_steps.size(); }
    std::string undo_label() const;   // "" when nothing
    // The node the last undo / redo is about -- select it so the user SEES
    // what came back. Empty if none.
    NodeId last_focus() const { return m_last_focus; }
    // s049. What was SELECTED when the last undone / redone step was made, if
    // the Shell said (set_selection_source). Several notes set together come
    // back selected together, so the next try reaches the same group.
    const std::vector<NodeId>& last_selection() const { return m_last_selection; }
    void set_selection_source(std::function<std::vector<NodeId>()> fn) { m_selection_of = std::move(fn); }
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
    // s046b: the gesture being built
    int                   m_depth = 0;
    UndoStep              m_open;
    std::vector<NodeId>   m_open_seen;
    std::function<double()> m_clock;
    std::function<void()>   m_changed;
    double now() const;
    void changed() const { if (m_changed) m_changed(); }
    NodeId                m_last_focus;
    std::vector<NodeId>   m_last_selection;              // s049
    std::function<std::vector<NodeId>()> m_selection_of;  // s049
    const NodeSource*     m_seen = nullptr;   // a different store: forget every step
    // A different store: forget every step (not the gesture being opened).
    void guard(const NodeSource& src) {
        if (&src != m_seen) { m_steps.clear(); m_at = 0; m_seen = &src; changed(); }
    }
};

// ── s046b: UndoSource -- the one door ──────────────────────────────────────
// A NodeSource that IS the store to every surface but the note body: reads
// pass straight through, and every write is recorded in the journal as a step
// (or as part of the gesture already open). A pane given this instead of the
// store is undoable without knowing undo exists -- Today's tick, the Inbox's
// Keep, Note details' calendar -- and a verb added next year is too. Every
// write in NodeSource is pure virtual, so a new one cannot be added without
// this class overriding it: the compiler is half the gate, the selftest's
// source scan (raw writes on the real store) is the other half.
//
// The editor alone gets the real store: the body's undo is the text view's.
class UndoSource : public NodeSource {
public:
    // `inner` is asked for the store at every call, so a folder swap needs no
    // re-pointing here (the Shell's store pointer is the one truth).
    UndoSource(Journal& j, std::function<NodeSource*()> inner)
        : m_j(j), m_inner(std::move(inner)) {}

    NodeSource* inner() const { return m_inner ? m_inner() : nullptr; }
    Journal&    journal() { return m_j; }

    std::vector<NodeId> children(const NodeId& parent) const override;
    const Node*         find(const NodeId& id) const override;
    std::size_t         count() const override;
    const std::vector<LogRecord>& history() const override;

    NodeId create(const NodeId& parent, const std::string& title) override;
    bool   set_title(const NodeId& id, const std::string& title) override;
    bool   set_body(const NodeId& id, const std::string& body) override;
    bool   set_protect(const NodeId& id, bool on) override;
    bool   set_task(const NodeId& id, const Task& t) override;
    bool   set_inbox(const NodeId& id, bool on) override;
    bool   set_packet(const NodeId& id, bool on) override;
    bool   set_sent(const NodeId& id, std::int64_t when, const std::string& to) override;
    bool   set_nudge(const NodeId& id, int days) override;
    using NodeSource::move;
    bool   move(const NodeId& id, const NodeId& new_parent, int index) override;
    bool   remove(const NodeId& id) override;
    bool   restore(const Node& n, int index) override;

private:
    Journal&                     m_j;
    std::function<NodeSource*()> m_inner;
};

// Run `fn` as ONE step called `label` when `src` is an UndoSource (touching
// `ids` first, with their subtrees when asked); otherwise just run it. What
// the tree, the Shell and a pane use to NAME a step or to fold several writes
// into one. Returns whether a step was kept.
bool as_step(NodeSource& src, const std::string& label, const std::vector<NodeId>& ids,
             bool subtrees, const std::function<void()>& fn, const std::string& merge = {});

// The same as a scope: every write through `src` until the end of the block
// is one step. For a verb with early exits and loops (Import, Clean Up).
class Gesture {
public:
    Gesture(NodeSource& src, const std::string& label, const std::string& merge = {});
    ~Gesture();
    Gesture(const Gesture&) = delete;
    Gesture& operator=(const Gesture&) = delete;
    void touch(const std::vector<NodeId>& ids, bool subtrees);
private:
    UndoSource* m_u = nullptr;
};

// ── s046b: the gate's other half ───────────────────────────────────────────
// Scan one UI source file's text for a write that goes AROUND the door: a
// write method called on the real store (`m_store->set_due(`), the real store
// handed to a core writer (`core::clean_up(*m_store`), or the real store
// handed to a pane (`set_source(m_store`). A line (or the line above it)
// carrying `// raw:` and a reason is allowed -- the editor's body, a relink
// that moved files on disk. Returns "file:line: text" per finding.
std::vector<std::string> raw_writes(const std::string& file, const std::string& text);

}  // namespace jot::core
