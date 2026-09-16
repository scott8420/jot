#pragma once
#include "core/Nodes.hpp"
#include "widgets/Widgets.hpp"

#include <giomm/menu.h>
#include <sigc++/signal.h>

#include <set>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// TreePane -- the node tree, and jot's answer-by-looking to D6.
//
// POLE B, flat. Every visible node gets one row widget; a structural change
// throws all of them away and rebuilds. No `TreeListModel`, no recycling
// factory, no virtualisation. Folio ran this shape for ~350 sessions with
// arbitrary depth, inline rename and drag-reorder, which is the same feature
// set jot needs.
//
// Pole B rests on one invariant -- THE TREE IS SMALL ENOUGH THAT A FULL REBUILD
// IS CHEAP -- and that invariant is measurable rather than arguable, so every
// rebuild logs its row count and its milliseconds to the `tree` area. The
// stress fixture (menu: Diagnostics) loads 5000 nodes so the number is a real
// one. If it is bad, D6 resolves to Pole A with evidence instead of taste.
//
// The flattening (rows in document order, indented, rather than nested boxes)
// is Pole B with GTK's ListBox doing selection and keyboard nav. It keeps the
// non-virtualised cost that D6 is actually asking about, and it means collapse
// is "don't emit the descendants" rather than a Revealer per group.
//
// It reads a core::NodeSource and NOTHING ELSE -- today that is fixtures in
// memory, later it is the store, and this file must not change at the swap.
// Split by category (CANON: the header is the index):
//   src/TreePane.cpp      -- glue + zones + the rebuild
//   src/TreePane_dnd.cpp  -- drag source, drop targets, the reparent decision
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class TreePane : public widgets::Box {
public:
    explicit TreePane(std::string_view name);
    // The context popover is set_parent()ed to the list, and GTK requires a
    // parented popover to be unparented before it is finalised -- otherwise the
    // teardown warns and the widget leaks.
    ~TreePane() override;

    void set_source(core::NodeSource* src);      // category: glue
    void rebuild();                              // category: zone: the full repaint (timed)
    void select(const core::NodeId& id);         // category: glue
    // Select a node that may not currently be on screen: expand every collapsed
    // ancestor, rebuild, then select. select() alone is a NO-OP on a row that
    // isn't emitted, which would make a link to a folded-away note do nothing
    // and say nothing.
    void reveal(const core::NodeId& id);         // category: glue

    // ── inline rename ───────────────────────────────────────────────────────
    // Double-click a row to rename in place. Implemented as a BUILD-ROW
    // VARIANT rather than widget surgery on a live row: begin_rename() records
    // which id is being renamed and rebuilds, and build_row() emits an Entry
    // instead of a Label for that one id. Pole B makes this nearly free -- a
    // rebuild is what this pane does for every other change too -- where under
    // Pole A it would be a factory-lifecycle problem.
    // The row context menu. Every item is an existing `win.*` action, so this
    // is a Gio::Menu plus a right-click gesture and NOT a second copy of the
    // verbs -- the greying logic in Shell::update_note_actions() already
    // decides what is an offer, and the menu inherits it for free.
    //
    // It closes a real discoverability hole: until s007, Delete was Ctrl+Delete
    // or two levels into the hamburger, and nothing on a row said so.
    void build_row_menu();                       // category: zone: the context menu model
    void popup_row_menu(double x, double y);     // category: helper: show it at the pointer

    void begin_rename(const core::NodeId& id);   // category: helper: rename in place
    void end_rename(bool commit);                // category: helper: commit or abandon
    bool renaming() const { return !m_renaming.empty(); }
    core::NodeId selected() const { return m_selected; }

    // Emitted when the user picks a row (not when selection is restored after a
    // rebuild -- the surface above should not reload the editor on every drag).
    sigc::signal<void(core::NodeId)>& signal_selected() { return m_sig_selected; }

private:
    // ── zones ──────────────────────────────────────────────────────────────
    void append_rows(const core::NodeId& parent, int depth);   // category: zone: recursive emit
    Gtk::Widget* build_row(const core::Node& n, int depth,
                           bool has_children);                 // category: zone: one row

    // ── dnd (src/TreePane_dnd.cpp) ─────────────────────────────────────────
    // Three zones per row, the shape Folio's binder uses: the top quarter means
    // "above this row", the bottom quarter "below", the middle "inside". Each
    // resolves to a (parent, index) pair before the model is asked anything.
    enum class DropZone { Above, Into, Below };

    DropZone zone_for(const Gtk::Widget& row, double y) const;              // category: dnd: geometry
    bool resolve_drop(const core::NodeId& target, DropZone z,
                      core::NodeId& parent, int& index) const;              // category: dnd: zone -> model coords
    void show_drop_feedback(Gtk::Widget* row, DropZone z);                  // category: dnd: the indicator
    void clear_drop_feedback();                                             // category: dnd: the indicator
    void attach_row_dnd(Gtk::Widget& row, const core::NodeId& id);  // category: dnd: source + target
    void attach_root_drop();                                        // category: dnd: empty space => top level
    bool drop_at(const core::NodeId& dragged, const core::NodeId& target,
                 DropZone z);                                    // category: dnd: the one model call

    // ── helpers ────────────────────────────────────────────────────────────
    void toggle_collapsed(const core::NodeId& id);   // category: helper: twisty
    bool is_collapsed(const core::NodeId& id) const;

    core::NodeSource*         m_src = nullptr;   // not owned; the seam
    core::NodeId              m_selected;
    std::set<core::NodeId>    m_collapsed;       // default is expanded
    std::vector<core::NodeId> m_row_ids;         // ListBox row index -> node id

    widgets::ScrolledWindow m_scroll;
    widgets::ListBox        m_list;

    // ONE popover, re-pointed and re-shown. A popover per row would be one
    // widget per node for a thing only ever open once.
    widgets::PopoverMenu    m_menu;
    Glib::RefPtr<Gio::Menu> m_menu_model;

    // Drag state. The dragged id is remembered at prepare-time so `motion` can
    // ask the model whether this drop would be legal and refuse the cursor if
    // not -- a refused drop the user can see before releasing, rather than a
    // log line after.
    // The row currently being renamed in place, if any. build_row() reads it.
    core::NodeId m_renaming;
    Gtk::Entry*  m_rename_entry = nullptr;   // not owned; managed by its row

    core::NodeId m_dragging;
    Gtk::Widget* m_feedback_row = nullptr;

    // True while rebuild() is repopulating: GTK fires row-selected during the
    // teardown and the restore, and neither is a user choice.
    bool m_rebuilding = false;

    sigc::signal<void(core::NodeId)> m_sig_selected;
};

}  // namespace jot
