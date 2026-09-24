#pragma once
#include "core/Links.hpp"
#include "core/Nodes.hpp"
#include "core/Tasks.hpp"
#include "widgets/Widgets.hpp"

#include <sigc++/signal.h>

#include <map>

#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// DrawerPane -- everything about the note that is not the note.
//
// The third pane, and the reason the body pane can now be alone on screen: once
// there is somewhere else for metadata to live, hiding both side panes leaves
// the blank note that ARCHITECTURE calls the front door. The toggles are that
// focus mode; they are not a side effect of having panes.
//
// WHAT IT CARRIES, and why each one is here rather than somewhere else:
//
//   Links out   -- nearly free, and the half jot already had. core::scan hands
//                  over the target string as of s006; this only resolves ids to
//                  titles and draws a row. Non-`jot:` targets (http, an
//                  attachment path) are shown as themselves and are not
//                  clickable -- promising a click we do not honour is worse
//                  than not offering one.
//   Backlinks   -- the half that is a feature. Comes from core::LinkIndex,
//                  which the Shell owns and keeps current, because the answer
//                  lives in every OTHER note's body and cannot be computed
//                  from the one in front of you.
//   Tags        -- READ-ONLY. D4 (where GTD state lives) is open, and an editor
//                  is a commitment to a storage location. Showing what the body
//                  already says commits nothing and is the cheapest way to look
//                  at inline tags before deciding whether they are the answer.
//   Structure   -- the parent's TITLE, the child count, the protected state.
//                  This is what the s005 status line carried, and the status
//                  line retires here rather than earlier: it existed to prove a
//                  drag preserved identity, that test held, and the information
//                  it carried had nowhere else to go until now.
//   File        -- notes/<uuid>.md, its size, when it was written. Note-level,
//                  deliberately: the JOTS FOLDER is app-level and belongs in
//                  the header, where s005 put it. Two altitudes, no collision.
//   Identity    -- COLLAPSED. Nobody recognises a note by 019f3e82-841e-77bc.
//                  The two real needs the id served are better served without
//                  showing it: making a link is a button, and correlating with
//                  the log is a developer need that folds away. THE PARENT ID
//                  DOES NOT APPEAR AT ALL, in any state -- the parent's title
//                  is the answer and the tree already shows where you are.
//
// SECTIONS (s016a). Everything below Name is a collapsible section with a
// clickable header, and every section is DECLARED IN ONE TABLE (kSections, top
// of DrawerPane.cpp): key, heading, open-by-default, hide-when-empty. Scott's
// direction was that properties had been appended to the bottom of this pane
// one milestone at a time; the table is the structural answer. A new property
// goes into a section that exists or gets a row in the table, and there is no
// longer a bottom to append to.
//
//   Name        -- PINNED, not a section. It is the pane's title as much as a
//                  field, and a folded-away name is a panel about nothing.
//   Todo        -- the task block.
//   Structure   -- where the note sits, its children, protection, and how the
//                  children run (Sequential / Parallel) -- a statement about
//                  the note's place in the tree, so it lives with the rest of
//                  that.
//   Links, Linked from, Tags -- as described above; hidden entirely when empty.
//   File, Identity -- reference, closed by default.
//
// Open or closed is remembered APP-WIDE (core::Prefs::drawer_open), and the
// drawer does not write prefs itself: it says which section changed and the
// Shell stores it. A section header carries a count where one means something
// ("Links 3"), so a closed section still answers "is there anything in here".
//
// It reads a core::NodeSource and a const core::LinkIndex& and nothing else. It
// owns neither. Clicking a link row emits `signal_goto` and the Shell decides
// what that means -- the drawer does not reach into the tree.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class DrawerPane : public widgets::Box {
public:
    explicit DrawerPane(std::string_view name);

    void set_source(core::NodeSource* src, const core::LinkIndex* index);
    void show_node(const core::NodeId& id);

    // A rename that happened SOMEWHERE ELSE (the tree, the editor's title).
    // Not show_node(): re-reading the whole node would rebuild every row and
    // move the cursor out from under whoever is typing. It also refuses to
    // touch the entry while the entry has focus, because the only way that
    // happens is that the drawer is the thing authoring the change.
    void title_changed_elsewhere(const std::string& title);
    void refresh();                                  // re-read the current node

    // The jots folder on disk, or empty in the scratch buffer. The drawer needs
    // it to name a file path, and it is told rather than asking -- resolving a
    // path from a NodeSource would mean knowing which implementation it is.
    void set_jots_dir(const std::string& dir);

    core::NodeId current() const { return m_id; }

    // Section open/closed state, keyed by section key. Keys not in the map take
    // the table's default. Called once at startup with what Prefs remembered.
    void set_section_states(const std::map<std::string, bool>& open);

    // A section header was clicked: key, and whether it is now open. The Shell
    // stores it; the drawer does not know a prefs file exists.
    sigc::signal<void(std::string, bool)>& signal_section_toggled() {
        return m_sig_section;
    }

    // A link row was activated. The Shell reveals and selects; the drawer does
    // not know the tree exists.
    sigc::signal<void(core::NodeId)>& signal_goto() { return m_sig_goto; }

    // "Copy link to this note" was pressed. The Shell owns the clipboard,
    // because a clipboard is a window-level thing.
    sigc::signal<void(core::NodeId)>& signal_copy_link() { return m_sig_copy_link; }

private:
    // One collapsible section (s016a). The header is a flat button: arrow,
    // heading, and a count on the right when the section has one. `body` is
    // what folds; `rows` is the part of it rebuilt on every refresh, so held
    // widgets (the task controls, the ordering radios) can live in `body`
    // beside it without being torn down under someone's cursor.
    struct Section {
        std::string       key;
        bool              hide_empty = false;   // hidden when it has nothing to say
        bool              open       = true;
        widgets::Box*     frame = nullptr;
        widgets::Button*  head  = nullptr;
        widgets::Image*   arrow = nullptr;
        widgets::Label*   title = nullptr;
        widgets::Label*   count = nullptr;
        widgets::Box*     body  = nullptr;
        widgets::Box*     rows  = nullptr;
    };

    Section add_section(const std::string& key);   // heading/defaults from kSections
    void    clear(Section& s);                     // rows emptied, count blanked
    void    apply_open(Section& s);                // arrow + body to match s.open
    void    set_count(Section& s, std::size_t n);  // "" at zero
    void    show_sections(bool on);
    void    write_title();
    Gtk::Widget* link_row(const std::string& label, const std::string& detail,
                          const core::NodeId& go_to, bool dangling);
    Gtk::Widget* fact_row(const std::string& text, bool dim);

    // ── the Task block ──────────────────────────────────────────────────────
    // The SECOND control in an otherwise all-report pane, and it sits directly
    // under Name for that reason: the two things you can change are together
    // at the top, and everything below them is derived.
    //
    // It writes through the NodeSource exactly as the Name field does -- never
    // into a task index, never into the tree. The index hears about it the same
    // way every other surface does, through the model's change notification,
    // which is what stops an edit here from needing to know Today exists.
    void build_task_block();
    void fill_task(const core::Node& n);
    void commit_date(core::DateKind kind);        // an entry -> the model
    void update_task_sensitivity(const core::Node& n);

    void fill_links(const core::Node& n);
    void fill_backlinks(const core::Node& n);
    void fill_tags(const core::Node& n);
    void fill_structure(const core::Node& n);
    void fill_file(const core::Node& n);
    void fill_identity(const core::Node& n);

    core::NodeSource*      m_src   = nullptr;   // not owned; the seam
    const core::LinkIndex* m_index = nullptr;   // not owned; the Shell keeps it current
    core::NodeId           m_id;
    std::string            m_jots_dir;

    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_column;
    widgets::Label          m_empty;     // "no note selected"

    // Rename, third way. The editor's big title entry is one, double-clicking a
    // tree row is another, and this is the one you reach for when the drawer is
    // already open and you are looking at the note's metadata. All three write
    // through NodeSource::set_title, and all three learn about each other the
    // same way -- via the model's change notification, never directly.
    widgets::Label m_name_head;
    widgets::Entry m_name;

    // In display order. Held by value; m_all points at them for the loops.
    Section m_todo_sec, m_structure, m_links, m_backlinks, m_tags, m_file, m_identity;
    std::vector<Section*> m_all;

    // True while show_node() is filling the entry: GTK fires `changed` on a
    // programmatic set, and writing that back would mark every note modified
    // just for being looked at. (The same stone EditorPane carries.)
    bool m_loading = false;

    // Task controls. Held (not rebuilt like the report rows) because they carry
    // focus and a cursor: rebuilding an entry under someone's hands is how a
    // date you were half-way through typing disappears.
    widgets::CheckButton m_todo;        // is this node a todo at all
    widgets::Box         m_task_body;   // everything that only applies once it is
    widgets::CheckButton m_done;
    widgets::CheckButton m_flag;
    widgets::Box         m_due_row;
    widgets::Label       m_due_label;
    widgets::Entry       m_due;
    widgets::Box         m_defer_row;
    widgets::Label       m_defer_label;
    widgets::Entry       m_defer;
    widgets::Label       m_avail;       // the DERIVED line: why it is or is not actionable

    // Children: how this node's children run. Shown only when it HAS children
    // (or already carries a status), because ordering nothing is not a choice
    // anybody needs offered.
    widgets::Box         m_order_row;
    widgets::Label       m_order_label;
    widgets::CheckButton m_order_none;
    widgets::CheckButton m_order_seq;
    widgets::CheckButton m_order_par;

    widgets::Label    m_uuid;
    widgets::Button   m_copy_link;

    sigc::signal<void(core::NodeId)> m_sig_goto;
    sigc::signal<void(core::NodeId)> m_sig_copy_link;
    sigc::signal<void(std::string, bool)> m_sig_section;
};

}  // namespace jot
