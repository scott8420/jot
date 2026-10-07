#pragma once
#include "core/Enclosures.hpp"
#include "core/Links.hpp"
#include "core/Nodes.hpp"
#include "core/Selection.hpp"
#include "core/Tasks.hpp"
#include "widgets/Widgets.hpp"

#include <gdkmm/texture.h>
#include <sigc++/signal.h>

#include <functional>
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
//   Tags        -- READ-ONLY: the body is the source (Scott, Oct 2026 -- inline
//                  #tags, the Obsidian way). s035: each tag is a chip; a click
//                  emits signal_tag and the Shell opens the Tags view on it.
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
//   Enclosures  -- (s016b) what the note CARRIES: one row per attached file,
//                  a thumbnail, its name, size, where it came from, and
//                  Missing when the note points at a file that is not there.
//                  Derived from the body's references (core::enclosures), so
//                  it cannot disagree with the note. Hidden when empty.
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
    // s049: the whole selection, in document order (s047 said only how many
    // others). Two or more and the pane shows what they SHARE -- a value every
    // one has as itself, one that differs as "mixed" -- and a set writes to all
    // of them as one step. One (or none) and it is the one note's pane.
    void set_selection(const std::vector<core::NodeId>& ids);

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

    // Where this note's enclosures live and what is known about them: the jots
    // folder's attachments/, or the scratch buffer's staging folder. Told, not
    // asked, for the same reason as the jots dir. Not owned; the Shell re-tells
    // it on every store swap, so it never outlives the store it points into.
    void set_attach(const core::AttachStore* store);

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

    // An enclosure row's menu (s017): what ("open", "reveal", "copy", "save")
    // and which file (its name within the store). The Shell acts -- it owns
    // the window a launcher or a save dialog needs, and the clipboard.
    sigc::signal<void(std::string, std::string)>& signal_enclosure_action() {
        return m_sig_enclosure;
    }

    // "Copy link to this note" was pressed. The Shell owns the clipboard,
    // because a clipboard is a window-level thing.
    sigc::signal<void(core::NodeId)>& signal_copy_link() { return m_sig_copy_link; }
    // s035: a tag chip clicked -- the tag's name as written, without the hash.
    sigc::signal<void(std::string)>& signal_tag() { return m_sig_tag; }
    // s035b: the tag field. A NAME already cleaned (core::clean_tag_name);
    // the Shell turns it into an edit of the tag line, through the editor so
    // it is one undo step.
    sigc::signal<void(std::string)>& signal_tag_add()    { return m_sig_tag_add; }
    sigc::signal<void(std::string)>& signal_tag_remove() { return m_sig_tag_remove; }
    // s037: "Reviewed" pressed. The Shell marks it (and walks on, in Review).
    sigc::signal<void()>& signal_reviewed() { return m_sig_reviewed; }
    // s051: Gather for sending -- "folder" or "zip" -- and "show" (the last
    // one made, in Files). The Shell asks where, gathers and stamps.
    sigc::signal<void(std::string)>& signal_gather() { return m_sig_gather; }

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
        Gtk::Widget*      cap   = nullptr;   // s045: the rows' own scroller, when capped
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
    // `picked`: from a picker, so an empty field means "clear it" even over a
    // mixed selection (typed, an empty mixed field means nothing was typed).
    void commit_date(core::DateKind kind, bool picked = false);   // an entry -> the model
    void commit_review();                         // s037: the Review entry -> the model
    Gtk::Widget* review_picker();
    void commit_repeat(bool picked = false);      // s033: the Repeat entry -> the model
    void commit_estimate(bool picked = false);    // s040: the Time entry -> the model
    Gtk::Widget* estimate_picker();               // s040
    // s033b: the dropdowns beside the fields. Each writes TEXT into its entry
    // and commits it, so a pick and a typed value take one road to the model.
    Gtk::Widget* date_picker(core::DateKind kind);
    Gtk::Widget* repeat_picker();
    void update_task_sensitivity(const core::Node& n);

    // ── s049: several at once ───────────────────────────────────────────────
    bool multi() const { return m_ids.size() > 1; }
    void show_many();                         // the pane for a selection of 2+
    void fill_task_common();                  // the Todo block from m_common
    void fill_tags_common();                  // shared chips, then "some" chips
    // Every writable note (todos only when asked) gets `fn`, as ONE step named
    // many_label(what, n). Returns how many it went to.
    std::size_t write_all(const std::string& what, bool todos_only,
                          const std::function<void(const core::NodeId&)>& fn);
    // Puts `text` in a field, or blank with "mixed" as its placeholder; a
    // single note's own placeholder comes back with `plain`.
    void show_field(widgets::Entry& e, bool mixed, const std::string& text, const char* plain);
    std::vector<core::NodeId> m_ids;          // the selection when 2+, else empty
    core::Common              m_common;       // what m_ids share, as of the last fill
    widgets::Label            m_many_note;    // in Todo: what a set reaches, and what it skips

    void fill_links(const core::Node& n);
    void fill_packet(const core::Node& n);   // s044
    void fill_done_when(const core::Node& n);   // s054: the count + rows when it is not a packet
    void fill_backlinks(const core::Node& n);
    void fill_tags(const core::Node& n);
    void fill_structure(const core::Node& n);
    void fill_file(const core::Node& n);
    void fill_identity(const core::Node& n);
    void fill_enclosures(const core::Node& n);
    Gtk::Widget* enclosure_row(const core::Enclosure& e);
    Glib::RefPtr<Gdk::Texture> thumbnail(const std::string& path);

    core::NodeSource*      m_src   = nullptr;   // not owned; the seam
    const core::LinkIndex* m_index = nullptr;   // not owned; the Shell keeps it current
    core::NodeId           m_id;
    std::string            m_jots_dir;
    const core::AttachStore* m_attach = nullptr;   // not owned; see set_attach

    // Thumbnails, by path, keyed on (mtime, size) so a replaced file is
    // re-read and an unchanged one is not. show_node() runs on EVERY
    // keystroke into the note; decoding a photo per keystroke would be the
    // drawer making the typing slow. Decoded AT thumbnail size, never full.
    struct Thumb {
        std::int64_t stamp = 0;
        std::int64_t size  = 0;
        Glib::RefPtr<Gdk::Texture> tex;
    };
    std::map<std::string, Thumb> m_thumbs;

    // s017 fix. An enclosure row's menu is a popover owned by a button IN a
    // rebuilt row. A rebuild while it is open destroys the button under its
    // own popover -- on Scott's GNOME/Wayland that was a gtk_widget_get_parent
    // CRITICAL and a hang. So while any row menu is open, a refresh is HELD
    // and runs once, on an idle, after the menu closes.
    int  m_menus_open      = 0;
    bool m_refresh_pending = false;

    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_column;
    widgets::Label          m_empty;     // "no note selected"

    // Rename, third way. The editor's big title entry is one, double-clicking a
    // tree row is another, and this is the one you reach for when the drawer is
    // already open and you are looking at the note's metadata. All three write
    // through NodeSource::set_title, and all three learn about each other the
    // same way -- via the model's change notification, never directly.
    widgets::Label m_also;        // s047: "+ 2 more selected ..."
    widgets::Label m_name_head;
    widgets::Entry m_name;

    // In display order. Held by value; m_all points at them for the loops.
    Section m_project_sec;   // s037b
    Section m_packet_sec;    // s044
    Section m_todo_sec, m_structure, m_links, m_backlinks, m_tags, m_enclosures, m_file,
            m_identity;
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
    widgets::Box         m_repeat_row;      // s033
    widgets::Label       m_repeat_label;
    widgets::Entry       m_repeat;
    widgets::CheckButton m_repeat_done;     // count from when it is done, not from the due date
    widgets::Box         m_est_row;         // s040: how long it takes
    widgets::Label       m_est_label;
    widgets::Entry       m_est;
    widgets::Label       m_avail;       // the DERIVED line: why it is or is not actionable

    // Children: how this node's children run. Shown only when it HAS children
    // (or already carries a status), because ordering nothing is not a choice
    // anybody needs offered.
    widgets::Box         m_order_row;
    widgets::Label       m_order_label;
    // s034: joined icon toggles (one group, exactly one pressed) instead of
    // four radios, with the pressed one's meaning spelled out under them.
    widgets::Box          m_order_buttons;
    widgets::ToggleButton m_order_none;
    widgets::ToggleButton m_order_seq;
    widgets::ToggleButton m_order_par;
    widgets::ToggleButton m_order_list;   // s031: single actions
    widgets::Label        m_order_says;   // s034: what the pressed one means

    // s031. Where this project stands. Shown on the same terms as Children --
    // a node with children, or one already carrying a state -- because a leaf
    // note being "on hold" is not a thing anybody needs offered.
    widgets::Box         m_state_row;
    widgets::Label       m_state_label;
    widgets::Box          m_state_buttons;   // s034, as Children
    widgets::ToggleButton m_state_active;
    widgets::ToggleButton m_state_hold;
    widgets::ToggleButton m_state_done;
    widgets::ToggleButton m_state_drop;
    widgets::Label        m_state_says;

    // s037. How often this project wants a look, when it had one, and the
    // button that says it just did. Shown when core::is_project says so.
    // s037b. The Project section: the switch, why it is (or is not) one when
    // nobody said, and -- once it is -- its dates, Status and Review.
    widgets::CheckButton m_proj_check;
    widgets::Label       m_proj_why;
    // s044 (J2): the packet -- a mark and what it adds up to.
    widgets::CheckButton m_packet_check;
    widgets::Label       m_packet_says;
    // s054 (J3): Done when -- the rule (a packet's is fixed at Items) and
    // what it means, above the packet strategy.
    widgets::Box         m_dw_row;
    widgets::Label       m_dw_label;
    widgets::DropDown    m_dw_pick;
    widgets::Label       m_dw_hint;
    // s051: Gather for sending -- the line it adds up to, the two ways out,
    // and the stamp once it has gone.
    widgets::Box         m_gather_row;
    widgets::Label       m_gather_label;
    widgets::Button      m_gather_folder;
    widgets::Button      m_gather_zip;
    widgets::Box         m_sent_row;
    widgets::Label       m_sent_says;
    widgets::Button      m_sent_show;
    widgets::Button      m_again;          // s053: Do it again
    // s052: how often it nudges while something is missing.
    widgets::Box         m_nudge_row;
    widgets::Label       m_nudge_label;
    widgets::DropDown    m_nudge_pick;
    sigc::signal<void(std::string)> m_sig_gather;
    widgets::Box         m_proj_body;
    widgets::Label       m_proj_dates_note;
    // Flag, Due and Defer: ONE set of controls, shown with the todo when the
    // note is one, else with the project. Re-homed between the two bodies at
    // fill time (only when the home changes) -- two editors of one field would
    // be two chances to disagree.
    widgets::Box         m_when_box;
    void place_when_box(bool in_task);

    widgets::Box         m_review_row;
    widgets::Label       m_review_label;
    widgets::Box         m_review_line;
    widgets::Entry       m_review_every;
    widgets::Button      m_review_btn;
    widgets::Label       m_review_says;
    sigc::signal<void()> m_sig_reviewed;

    widgets::Label    m_uuid;
    widgets::Button   m_copy_link;

    sigc::signal<void(core::NodeId)> m_sig_goto;
    sigc::signal<void(std::string)>  m_sig_tag;   // s035
    sigc::signal<void(std::string)>  m_sig_tag_add;      // s035b
    sigc::signal<void(std::string)>  m_sig_tag_remove;   // s035b
    // s035b: the add row lives in the Tags section's BODY, under its rows, and
    // is HELD -- the rows are rebuilt on every refresh, and a field rebuilt
    // under your typing would lose the focus and the text.
    widgets::Box*     m_tag_add_row   = nullptr;
    widgets::Entry*   m_tag_entry     = nullptr;
    widgets::Box*     m_tag_pick_col  = nullptr;
    widgets::Popover* m_tag_pick_pop  = nullptr;
    void build_tag_field();
    void fill_tag_picks();
    void commit_tag_entry();
    sigc::signal<void(core::NodeId)> m_sig_copy_link;
    sigc::signal<void(std::string, bool)> m_sig_section;
    sigc::signal<void(std::string, std::string)> m_sig_enclosure;
};

}  // namespace jot
