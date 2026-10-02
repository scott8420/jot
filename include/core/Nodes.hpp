#pragma once
#include "core/Repeat.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Nodes -- jot's one entity, and THE SEAM the surface reads through.
//
// Two things live here and they are deliberately separate:
//
//   Node        -- the data. A flat set; the tree is a PROJECTION of it,
//                  reconstructed by parent lookup. A move is one field write
//                  (`parent_id`) and children come along because they never
//                  referenced anything but their parent.
//
//   NodeSource  -- the interface. Everything above this line (tree, editor,
//                  DnD) talks to a NodeSource and to nothing else. Today the
//                  only implementation is MemoryNodes, filled from hardcoded
//                  fixtures with no persistence whatsoever. When the store
//                  lands it implements the SAME interface and the surface does
//                  not change by a line. (CANON: "fixture data through the real
//                  seam".) If the swap forces a surface change, the seam was
//                  drawn in the wrong place -- that's a finding, cheaply.
//
// Notr is the specimen this guards against: its model was this exact shape and
// its drag handler ignored it, copying a node and deleting the original through
// the tree widget's API. It could not move a subtree, and it minted a new
// identity on every drag. (CANON: "tracking is an illusion; the model is the
// truth.") Hence: there is no widget-side move. `move()` is a model verb.
//
// GTK-free by construction -- jot_core has no gtkmm on its link line, so a
// stray UI include here fails to compile and the selftest can exercise all of
// it headless.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// Stable identity, generated once, never reassigned. A string because the real
// store will mint uuids and ids must survive a save/load round trip unchanged.
using NodeId = std::string;

// ── the task half of a node (D2: a todo IS a node) ──────────────────────────
// How a node's CHILDREN run. On the parent, never on the child, because it is a
// statement about the set: Sequential means only the first incomplete one is
// available; Parallel means they all are. This is the whole GTD engine -- with
// defer dates it makes "what should I do now" computable instead of a wall.
//
// It is independent of `is_task`: a plain NOTE called "Taxes" is the natural
// container for a sequence of todos, and requiring the container to be a todo
// itself would put a checkbox on every project heading.
//
// s031: SingleActions -- OmniFocus's single-action list. A parent that is a
// bag of loose todos rather than a project with an end: every child is
// available (as Parallel), and it has no "next action" to show, because
// nothing in it comes first.
enum class Status { None, Sequential, Parallel, SingleActions };

// s031. Where a project STANDS -- on the parent, like Status, and like Status
// independent of is_task, because a plain note called "Kitchen" is the natural
// project and must be able to go on hold without growing a checkbox.
//
//   Active    -- the default; nothing changes.
//   OnHold    -- paused: nothing inside it is offered (Today, Available, the
//                due notification), but a date or a flag still shows it.
//   Completed -- finished: everything inside counts as done. On a TODO this is
//                stored as `done` (see core::set_project_state) so a project
//                never has two ways to be finished.
//   Dropped   -- given up on: kept, read, searchable, but out of every list.
enum class ProjectState { Active, OnHold, Completed, Dropped };

// The optional fields that make a node a todo. D2 says a node is a todo when it
// HAS them and a note when it does not -- and since C++ has no absent `bool`,
// `is_task` is that "has them". A todo with no dates and no flag is the common
// case and it must be representable, so the presence bit is explicit rather
// than inferred from a date being non-zero.
//
// NO PRIORITY FIELD, deliberately. Numeric priorities rot -- within a month
// everything is a 1, which is why OmniFocus dropped them. jot uses the three
// things that do not rot: SIBLING ORDER (already model state since s002, so
// "drag to prioritise" is already built), `flagged`, and `defer`.
struct Task {
    bool         is_task = false;  // the presence bit: this node is a todo
    bool         done    = false;
    std::int64_t due     = 0;      // epoch seconds, 0 == none
    std::int64_t defer   = 0;      // do not surface it before this
    bool         flagged = false;  // "this one, today"
    Status       status  = Status::None;   // how THIS node's children run
    ProjectState project = ProjectState::Active;   // s031: where THIS project stands
    // s032. WHEN it was finished: ticked (a todo), Completed or Dropped (a
    // project). 0 == not finished, or finished before s032 kept the time.
    // Nobody sets it by hand: MemoryNodes::set_task stamps it on the way IN to
    // a finished state and clears it on the way out, so every road to "done"
    // (the tree's tick, Today's tick, the drawer, the project menu) records
    // the time without knowing it does.
    std::int64_t finished = 0;
    // s033. Comes back when ticked (core/Repeat). Off by default.
    Repeat       repeat;

    bool operator==(const Task&) const = default;
};

struct Node {
    NodeId       id;
    NodeId       parent_id;        // empty => a root
    std::string  title;
    std::string  body;             // markdown; media referenced inline
    std::int64_t created  = 0;     // epoch seconds
    std::int64_t modified = 0;
    bool         protect  = false; // carried from Notr: locked against move/delete/edit
    Task         task;             // default-constructed => this node is a note
    // s028: waiting to be processed. Set by every capture road; cleared by
    // Clean Up once the node has been dealt with (core/Inbox), or by hand.
    // A MARK, not a place: the node stays wherever it is in the tree, and
    // filing it does not take it out of the Inbox until Clean Up says so --
    // the OmniFocus behaviour, so a row never vanishes from under the hand
    // that is processing it.
    bool         inbox    = false;
};

// s033. One occurrence of a repeating todo, done. The note itself rolls on to
// the next occurrence (core/Repeat), so the past lives here: the Logbook's
// record of it. The title is copied, not looked up, so a record still reads
// right after the note is renamed or deleted.
struct LogRecord {
    NodeId       id;
    std::string  title;
    std::int64_t when = 0;
    bool operator==(const LogRecord&) const = default;
};

// ── The seam ────────────────────────────────────────────────────────────────
// Reads are by id; there is no "give me your vector". Writes return false when
// the model refuses (a cycle, a protected node, an unknown id) -- refusal is a
// normal answer, not an exception, because a drag onto an illegal target is a
// thing users do constantly.
class NodeSource {
public:
    virtual ~NodeSource() = default;

    // ── reads ───────────────────────────────────────────────────────────────
    // children("") == the roots. Sibling order is the source's own and stable.
    virtual std::vector<NodeId> children(const NodeId& parent) const = 0;
    virtual const Node*         find(const NodeId& id) const         = 0;
    virtual std::size_t         count() const                        = 0;
    // s033: done occurrences of repeating todos, oldest first. Empty by default.
    virtual const std::vector<LogRecord>& history() const;

    // ── writes ──────────────────────────────────────────────────────────────
    virtual NodeId create(const NodeId& parent, const std::string& title) = 0;
    virtual bool   set_title(const NodeId& id, const std::string& title)  = 0;
    virtual bool   set_body(const NodeId& id, const std::string& body)    = 0;
    virtual bool   set_protect(const NodeId& id, bool on)                 = 0;
    // ONE virtual for the whole task record, not six. Every task edit is the
    // same operation to a store -- read the node, change some fields, mark it
    // dirty -- and six virtuals would be six chances for one override to forget
    // to persist. The single-field writers below are non-virtual conveniences
    // built on this one, so there is exactly one path to disk.
    virtual bool   set_task(const NodeId& id, const Task& t)              = 0;
    // s028. Not refused on a protected node: the mark is about the user's
    // processing, not the note's content, and a locked note can still be
    // something you have dealt with. Change::Flags; `modified` is untouched.
    virtual bool   set_inbox(const NodeId& id, bool on)                   = 0;

    bool set_done(const NodeId& id, bool on);
    bool set_flagged(const NodeId& id, bool on);
    bool set_due(const NodeId& id, std::int64_t when);
    bool set_defer(const NodeId& id, std::int64_t when);
    bool set_status(const NodeId& id, Status s);
    bool set_repeat(const NodeId& id, const Repeat& r);   // s033
    // Make this node a todo / stop it being one. Un-making CLEARS the fields
    // rather than leaving them set-but-ignored: a due date that survives
    // un-tasking is a date that comes back from the dead when you re-task.
    bool make_task(const NodeId& id, bool on);
    // index is the position among the new parent's children; -1 appends.
    // Sibling ORDER is model state, not view state: a drop between two rows has
    // to mean something the store can write down, or the indicator would be
    // drawing a promise the model can't keep.
    virtual bool   move(const NodeId& id, const NodeId& new_parent, int index) = 0;
    bool           move(const NodeId& id, const NodeId& new_parent) {
        return move(id, new_parent, -1);
    }
    virtual bool   remove(const NodeId& id)                               = 0;  // subtree

    // ── change notification ─────────────────────────────────────────────────
    // One callback, fired after any successful write, carrying WHAT changed and
    // WHICH node (id empty for a wholesale reload). The surface subscribes and
    // redraws; it never assumes it was the author of the change, because the
    // real store will also change underneath it (import, undo, a watcher).
    //
    // The kind is not decoration: a tree redraw is a full rebuild, and a Body
    // change must not trigger one or every keystroke in the editor repaints the
    // whole tree. A listener that ignores the kind still behaves correctly --
    // just slowly -- which is the right failure mode for an enum like this.
    // Task is its own kind rather than riding on Flags: a task edit changes what
    // is AVAILABLE, which can change rows in a view that is nowhere near the
    // node that moved (a sequential sibling three rows down becomes the next
    // action). Flags redraws one row; Task re-asks the index.
    enum class Change { Reload, Created, Removed, Moved, Title, Body, Flags, Task };

    using ChangedFn = std::function<void(Change, const NodeId&)>;
    void on_changed(ChangedFn fn) { m_changed = std::move(fn); }

protected:
    void notify(Change what, const NodeId& id) const { if (m_changed) m_changed(what, id); }

private:
    ChangedFn m_changed;
};

// ── Projections over any source (pure, no implementation knowledge) ─────────
// Walks parent links upward. `ancestor` empty => always true (the root scope).
bool is_descendant(const NodeSource& src, const NodeId& id, const NodeId& ancestor);

// Position of a node among its siblings; -1 if it isn't there. What a
// drop-above / drop-below gesture needs in order to name an index.
int sibling_index(const NodeSource& src, const NodeId& id);

// The rules a move must obey: a node may not become its own descendant, and a
// protected node does not move. Re-ordering within the same parent IS a legal
// move, so there is no "already there" refusal here. `why` (optional) gets a
// short reason, which is what the log line and the refused-drop cursor mean.
bool can_move(const NodeSource& src, const NodeId& id, const NodeId& new_parent,
              std::string* why = nullptr);

// ── capture ─────────────────────────────────────────────────────────────────
// Turning a line someone typed into a note, WITHOUT losing any of it.
//
// Three cases, and the rule is that the title and the body between them always
// hold the whole of what was typed:
//
//   multi-line          title = the first line; body = everything after it
//   one short line      title = the line; body empty
//   one very long line  title = it, cut at a word boundary with an ellipsis;
//                       body = THE WHOLE LINE, so the cut costs nothing
//
// The long-line case is the one that matters. A capture box invites a sentence,
// a sentence makes a useless tree row, and truncating without keeping the
// original would mean the app silently ate the end of a thought -- which is the
// one thing a capture box must never do.
void capture_split(const std::string& text, std::string& title, std::string& body);

// Capture `text` as a new TOP-LEVEL note, marked for the Inbox (s028), and
// return its id. s007 said "there is no Inbox"; s028 overturned it -- the
// mark is what makes a capture something to PROCESS rather than a note that
// happens to sit at the top level. Every capture road goes through here or
// through capture_list / capture_append, and each marks a note it MAKES (not
// one it grows: appending to Groceries is not a new thing to process).
//
// One function for both callers (the headerbar box and `jot --capture`) on
// purpose: two capture paths that split text differently would be two apps.
NodeId capture(NodeSource& src, const std::string& text);

// ── a list from the command line (s025c) ────────────────────────────────────
// `jot --list Groceries milk eggs "sourdough bread"` (Scott): a name and a set
// of items. Each item becomes a TASK line (`- [ ] milk`) -- a list you can
// tick off in jot. If a note with that title already exists it GROWS; else a
// new top-level note, the same place a capture lands.
//
// "That title": trimmed, case-insensitive (ASCII), the first match in tree
// order. A protected note is not a match -- it is read-only, so a second
// note is made rather than writing into it.
//
// Items are cleaned: trimmed, a newline inside one becomes a space, an empty
// one is dropped. No name or no items -> nothing done, empty id.
// `appended` says which of the two happened.
NodeId capture_list(NodeSource& src, const std::string& name,
                    const std::vector<std::string>& items, bool* appended = nullptr);

// s025d: `jot Groceries -a check the pantry` -- one line of plain text on the
// end of the note NAME, grown or made exactly as capture_list does. The line
// never joins a list above it: after a list line a blank line comes first
// (markdown would otherwise read it as that item's continuation).
NodeId capture_append(NodeSource& src, const std::string& name, const std::string& text,
                      bool* appended = nullptr);
std::string append_text(const std::string& body, const std::string& text);

// The pure half: `body` with the items added as task lines, on a fresh line.
std::string append_tasks(const std::string& body, const std::vector<std::string>& items);

// The note titled `title` as capture_list means it, or empty.
NodeId find_list_note(const NodeSource& src, const std::string& title);

// Model-side dump -- the sibling of registry::dump(), other layer. Ids and
// parents, indented by depth. This is what you want the first time a link
// resolves to nothing.
std::string dump(const NodeSource& src);

// ── MemoryNodes -- the only implementation today ────────────────────────────
// A flat vector plus a parent->children index, so children() is a lookup rather
// than a scan (it is called once per node on every rebuild). NO PERSISTENCE:
// nothing here reads or writes a file, by design for s002.
class MemoryNodes : public NodeSource {
public:
    std::vector<NodeId> children(const NodeId& parent) const override;
    const Node*         find(const NodeId& id) const override;
    std::size_t         count() const override;

    NodeId create(const NodeId& parent, const std::string& title) override;
    bool   set_title(const NodeId& id, const std::string& title) override;
    bool   set_body(const NodeId& id, const std::string& body) override;
    bool   set_protect(const NodeId& id, bool on) override;
    bool   set_task(const NodeId& id, const Task& t) override;
    bool   set_inbox(const NodeId& id, bool on) override;
    using NodeSource::move;   // keep the 2-arg convenience visible through this type
    bool   move(const NodeId& id, const NodeId& new_parent, int index) override;
    bool   remove(const NodeId& id) override;

    // Bulk load used by the fixtures; clears everything first and notifies once
    // with an empty id (the "everything changed" shape a real reload will use).
    void reset(std::vector<Node> nodes);

    // s032. The clock behind `modified` and the finish stamp. The selftest
    // stands it at a known instant; nothing else sets it.
    void set_clock(std::function<std::int64_t()> fn) { m_clock = std::move(fn); }

    // s033. The repeat history. set_history is the LOAD path: no notify.
    const std::vector<LogRecord>& history() const override { return m_history; }
    void set_history(std::vector<LogRecord> h) { m_history = std::move(h); }

protected:
    // Mint the id for a new node. MemoryNodes uses a counter because its nodes
    // are throwaway fixtures; a persistent store overrides this with something
    // that survives a save (Project mints uuids, which become filenames).
    virtual NodeId mint_id();

private:
    std::int64_t now() const;
    std::function<std::int64_t()> m_clock;
    std::vector<LogRecord>        m_history;
    Node* mutable_find(const NodeId& id);
    void  reindex();
    void  collect_subtree(const NodeId& id, std::vector<NodeId>& out) const;

    std::vector<Node>                                  m_nodes;
    std::unordered_map<NodeId, std::size_t>            m_by_id;   // id -> index
    std::unordered_map<NodeId, std::vector<NodeId>>    m_kids;    // parent -> ordered ids
    int                                                m_next = 1;
};

// ── Fixtures ────────────────────────────────────────────────────────────────
// Hardcoded. Thrown away the day the store lands. They exist so the surface has
// something to render on day one and so D1/D2/D6 can be answered by looking.
std::vector<Node> fixture_nodes();

// The D6 instrument: a synthetic set of `count` nodes in a plausible shape,
// so "is a full rebuild cheap enough?" is a measurement instead of an argument.
std::vector<Node> stress_nodes(int count);

}  // namespace jot::core
