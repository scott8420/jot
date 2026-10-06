#pragma once
#include "core/Import.hpp"
#include "JotsFolderDialog.hpp"
#include "MoveDialog.hpp"
#include "core/Nodes.hpp"
#include "core/Links.hpp"
#include "core/Undo.hpp"
#include "core/Tasks.hpp"
#include "Desktop.hpp"
#include "Notifier.hpp"
#include "core/Lifecycle.hpp"
#include "core/Notify.hpp"
#include "core/Prefs.hpp"
#include "core/Enclosures.hpp"
#include "core/Project.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/applicationwindow.h>
#include <gtkmm/headerbar.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>

#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Shell -- the app window: tree on the left, note on the right.
//
// This class is split across multiple TUs by CATEGORY OF CODE, not by feature
// (CANON: "vocabulary first, file layout second" + "the header is the index").
// The split costs ~nothing while the parts are small; retrofitting it after a
// class grows to thousands of lines is a multi-session migration, so the
// structure is installed on day one. The header below IS the index -- every
// method carries a trailing `// category: ...` tag naming the file its body
// lives in. Read the tag, open that file.
//
// Vocabulary
// ----------
//   Zones    -- construction of named regions: window frame, header, the
//               paned tree|editor body, the menu model.
//               File: src/Shell_zones.cpp
//   Bindings -- what's wired to what: Gio actions, the recents action group,
//               the model's change callback.  File: src/Shell_bindings.cpp
//   Handlers -- slot bodies (on_*): what a user action actually does.
//               File: src/Shell_handlers.cpp
//   Helpers  -- reusable sync/orchestration called from zones/bindings/handlers.
//               File: src/Shell_helpers.cpp
//   Glue     -- what stays in Shell.cpp: the ctor + build_ui() (the ordered
//               construct -> name/register -> bind pass).
//
// Finding things
// --------------
//   "Where is the window built?"         -> Shell_zones.cpp    (build_shell)
//   "Where are actions registered?"      -> Shell_bindings.cpp (bind_actions)
//   "What happens when the model moves?" -> Shell_handlers.cpp (on_model_changed)
//   "Where does the recents list sync?"  -> Shell_helpers.cpp  (note_recent / rebuild_*)
//
// THE SWAP POINT: `m_store` below is the only member that names a concrete
// NodeSource implementation. Everything else -- TreePane, EditorPane, every
// handler here -- holds a core::NodeSource*. When persistence lands it is
// constructed in place of MemoryNodes and nothing else in the UI changes. If
// something else has to change, the seam was drawn wrong, which is a finding.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class AboutWindow;
class ShortcutsDialog;
class CheatSheetWindow;
class PreferencesWindow;
class TreePane;
class EditorPane;
class DrawerPane;
class TodayPane;
class InboxPane;
class TagsPane;
class ProjectsPane;
class SearchPane;
class PerspectiveDialog;

class Shell : public Gtk::ApplicationWindow {
public:
    Shell();
    ~Shell() override;

    void build_ui();                             // category: glue

    // ── the two capture entry points ────────────────────────────────────────
    // Both are called from OUTSIDE the window: App::on_command_line hands them
    // whatever `jot --capture` was given. They are public for that reason and
    // for no other -- everything else the Shell does is reached through an
    // action.
    //
    // capture() does NOT move the selection, does not load the new note into
    // the editor, and does not present the window. Capture that costs you the
    // place you were working in is capture you stop using mid-thought, which is
    // the only time it matters.
    void capture(const std::string& text);       // category: helper: text -> an unfiled note
    // s025c: `jot --list NAME items...` -- task lines on the note NAME, grown if
    // it exists, made if not (core::capture_list). Returns the tell the
    // terminal prints: "Added 3 items to Groceries." / empty if nothing done.
    std::string capture_list(const std::string& name,
                             const std::vector<std::string>& items);  // category: helper
    // s025d: `jot NAME -a words` -- one line of text on the note NAME
    // (core::capture_append). Same return shape as capture_list.
    std::string capture_append(const std::string& name, const std::string& text);  // category: helper
    void focus_capture_bar();                    // category: helper: put the cursor in the box

    // Reveal and select a node from OUTSIDE the window -- what App dispatches a
    // due notification's click to. A thin public door onto the same
    // reveal-then-select that drawer links already use, rather than a second
    // path that could drift from it.
    void goto_note(const core::NodeId& id) { on_goto_note(id); }

    // s041: a notification button (`app.notice`), applied -- a verb and an
    // announce key, checked against the model before anything is written.
    // Returns the status-line sentence. after_cold_notice() is for the jot
    // GNOME started just to press the button: it leaves unless kept running.
    std::string notice_act(const std::string& param);   // category: helper
    void after_cold_notice();                           // category: helper

    // ── the one door out of the process ────────────────────────────────────
    // Public for the same reason the two capture doors are: it is called from
    // OUTSIDE the window. `app.quit` is what GNOME's Background Apps list
    // activates over D-Bus, and that arrives at the Application, not here.
    //
    // It may PROMPT before it quits, and if it does it presents the window
    // first -- a resident jot with unsaved notes has nowhere else to put the
    // question. So this is not "quit"; it is "begin quitting", and it can end
    // in the user cancelling.
    void request_quit();                         // category: helper: ask what needs asking, then go

private:
    // ── zones ──────────────────────────────────────────────────────────────
    void build_shell();                          // category: zone: window + header + paned body
    Glib::RefPtr<Gio::Menu> build_menu();        // category: zone: hamburger model
    void build_jots_title();                     // category: zone: the header title menu button
    void build_pane_toggles(Gtk::HeaderBar& header);  // category: zone: the two focus-mode toggles
    void build_left_pane();                      // category: zone: Notes | Today, one stack
    void build_capture_bar(Gtk::HeaderBar& header);  // category: zone: the capture line

    // ── bindings ───────────────────────────────────────────────────────────
    void bind_actions();                         // category: bindings: actions + recents group + model callback

    // ── handlers ───────────────────────────────────────────────────────────
    void on_new_note();                          // category: handler: new top-level note
    void on_new_child();                         // category: handler: new note under the selection
    void on_delete_note();                       // category: handler: delete the selected subtree
    void on_toggle_protect();                    // category: handler: lock/unlock the selection
    void on_capture_activate();                  // category: handler: Enter in the capture line
    void on_toggle_todo();                       // category: handler: make the selection a todo (or not)
    void on_toggle_done();                       // category: handler: tick/untick the selection
    void on_toggle_flag();                       // category: handler: flag/unflag the selection
    void on_rename_note();                       // category: handler: rename in the tree (F2)
    void on_left_view(const Glib::ustring& which);  // category: handler: Notes | Inbox | Today | Tags
    void on_show_tag(const std::string& tag);   // category: handler: s035 a #tag clicked -> the Tags view, picked
    void on_find();                              // category: handler: s038 Ctrl+F -> the Find field over the Notes tab
    void on_search_open(const core::NodeId& id, int cp, int len);   // category: handler: s038 a search hit -> the note, the match selected
    void end_search();                           // category: helper: s038 empty the Find field, the tree back
    void queue_search_refresh();                 // category: helper: s038 results follow the model, debounced, only while showing
    // s038b: perspectives -- the Find field's filter menu and saved queries.
    void build_find_menu();                      // category: helper: s038b the filter menu, perspectives listed
    void sync_find_actions();                    // category: helper: s038b the menu's radios read the field's text
    void set_find_text(const std::string& q);    // category: helper: s038b the one writer of the field from code
    void on_find_show(const Glib::ustring& show);   // category: handler: s038b Show: Anything / Remaining / ...
    void on_find_due(const Glib::ustring& due);     // category: handler: s038b Due: any time / overdue / ...
    void on_find_flagged();                      // category: handler: s038b Flagged only
    void on_find_est(const Glib::ustring& est);  // category: handler: s040 Time: any / 30 min / ...
    void on_open_perspective(const std::string& name);   // category: handler: s038b its query in the field
    void on_save_perspective();                  // category: handler: s038b Save as Perspective...
    void on_delete_perspective();                // category: handler: s038b the one the field shows
    void on_perspectives();                      // category: handler: s038b Ctrl+J: the filter menu, open
    void on_show_projects();                     // category: handler: s037 Ctrl+Shift+P -> Projects (again: Review <-> All)
    void on_mark_reviewed();
    void on_packet_gather(std::string how);  // category: handler: s051 -- Gather for sending ("folder" / "zip" / "show")
    void on_toggle_project();                    // category: handler: s037b the selection is / is not a project, on purpose
    void on_new_project();                       // category: handler: s037b Projects tab + -> a new top-level project, named in the tree                     // category: handler: s037 Ctrl+Shift+R -> the selection reviewed, on to the next
    void on_show_tags();                         // category: handler: s035 Ctrl+Shift+T -> the Tags view
    void on_tag_edit(bool add, const std::string& name);  // category: handler: s035b Note details adds / removes on the tag line
    void on_clean_up();                          // category: handler: s028 clear processed Inbox marks
    void on_project_state(const Glib::ustring& which);   // category: handler: s031 Active / On hold / Completed / Dropped
    void on_toggle_inbox();                      // category: handler: s028 the selection in / out of the Inbox
    // s045: the navigator's undo, and the outliner verbs (tree keys).
    void on_undo_nav();                          // category: handler: Ctrl+Z in the tree / Edit menu
    void on_redo_nav();                          // category: handler: Ctrl+Shift+Z / Ctrl+Y
    void on_new_sibling();                       // category: handler: Enter in the tree
    void on_indent();                            // category: handler: Tab in the tree
    void on_outdent();                           // category: handler: Shift+Tab in the tree
    void on_move_up();                           // category: handler: Alt+Up in the tree
    void on_move_down();                         // category: handler: Alt+Down in the tree
    void on_move_to();                           // category: handler: s029 "Move to..." on the selection
    // s047: `many` (the selection's roots) all go where the picker says; empty = just `id`.
    void open_move(const core::NodeId& id, std::vector<core::NodeId> many = {});   // category: handler: s029 the picker, for any note (Inbox rows too)
    void toggle_many(const char* on_word, const char* off_word,
                     const std::function<bool(const core::Node&)>& is_on,
                     const std::function<bool(const core::Node&)>& applies,
                     const std::function<void(const core::NodeId&, bool)>& set);   // category: helper: s047, a toggle verb over the selection
    void on_selection_changed(const core::NodeId& id);  // category: handler: tree row -> editor
    void on_model_changed(core::NodeSource::Change what,
                          const core::NodeId& id);      // category: handler: model -> surfaces
    void on_dump_nodes();                        // category: handler: print the model tree
    void on_new_jots();                          // category: handler: name a new jots folder
    void on_open_in_files();                     // category: handler: show the jots folder in the file manager
    void on_copy_path();                         // category: handler: jots folder path -> clipboard
    void on_relocate_jots();                     // category: handler: chooser -> move the whole jots folder
    void on_rename_jots();                       // category: handler: rename in place (a move that stays put)
    void on_open_jots();                         // category: handler: folder chooser -> open those jots
    void open_jots_chooser();                    // category: handler: the chooser itself, past the scratch guard
    void on_save_all();                          // category: handler: land every deferred write now
    void on_save_as();                           // category: handler: write a second jots folder and switch to it
    void on_recent_open(const std::string& p);   // category: handler: open a recent jots folder (parameterised)
    void on_recent_clear();                      // category: handler: empty the recents list
    void on_dump_registry();                     // category: handler: print the live widget tree
    void on_test_notify();                       // category: handler: four notifications, one field apart (log mode)
    void on_about();                             // category: handler: open the About window (dialog-lifetime exemplar)
    void on_preferences();                       // category: handler: open the preferences window
    void on_cheat_sheet();                       // category: handler: the cheat sheet (s030; renders from core::cheat_sheet)
    void on_shortcuts();                         // category: handler: keyboard reference (renders from core::shortcut_registry)
    void on_toggle_tree();                       // category: handler: show/hide the tree
    void on_toggle_drawer();                     // category: handler: show/hide the metadata drawer
    void on_toggle_reading();                    // category: handler: s021 Source <-> Reading
    void on_toggle_live();                       // category: handler: s022 Live Preview on/off
    void on_view_mode(const Glib::ustring& m);   // category: handler: s034 Source | Live | Reading, the joined control
    void build_view_modes(Gtk::HeaderBar& header);  // category: zone: s034 the three-way view control
    void on_import_markdown();                   // category: handler: s021b Notes -> Import Markdown Files...
    void on_import_folder();                     // category: handler: s021c Notes -> Import Markdown Folder...
    core::NodeId import_file(const std::string& path, const core::NodeId& parent,
                             std::vector<std::string>& failed);  // category: helper: s021c one file -> one note
    core::NodeId import_item(const core::ImportItem& item, const core::NodeId& parent,
                             std::vector<std::string>& failed, int& made);  // category: helper: s021c a plan -> notes
    void import_files(const std::vector<std::string>& paths,
                      const core::NodeId& parent);  // category: helper: s021b each .md becomes a note
    void on_edit_requested(int source_cp);       // category: handler: s021 the reading view asked to edit here
    void on_read_link(std::string target);       // category: handler: s021 a link clicked in Reading
    void on_toggle_desktop();                    // category: handler: turn the desktop projection on/off
    void on_toggle_notify();                     // category: handler: turn due notifications on/off
    void on_toggle_background();                 // category: handler: stay running with no window, or don't
    void on_desktop_sync_now();                  // category: handler: rewrite the desktop list now
    void on_goto_note(const core::NodeId& id);   // category: handler: a drawer link row -> reveal + select
    void on_copy_link(const core::NodeId& id);   // category: handler: [Title](jot:<id>) -> clipboard
    void on_files_dropped(std::vector<std::string> paths, int offset, bool flip);  // category: handler: files dropped on the body -> enclosures
    void on_image_pasted(std::string png, int offset);  // category: handler: a clipboard picture -> an enclosure
    void on_enclosure_action(std::string verb, std::string name);  // category: handler: open / reveal / copy / save one enclosure

    // ── helpers ────────────────────────────────────────────────────────────
    void note_recent(const std::string& path);   // category: helper: push onto the list + persist
    void rebuild_recents_menu();                 // category: helper: repaint the submenu
    std::string recents_file() const;            // category: helper: the XDG path (UI-side resolution)
    std::string default_jots_location() const;  // category: helper: the folder a NEW jots folder is offered in
    void update_jots_title();                    // category: helper: repaint the header title + its menu
    void name_jots(JotsFolderDialog::Mode mode, const std::string& location,
                   const std::string& name,
                   JotsFolderDialog::Done done);  // category: helper: the one naming dialog, three occasions
    bool scratch_has_content() const;            // category: helper: has anything been written with no folder?
    void save_scratch(const std::string& target);  // category: helper: adopt the scratch buffer into a new folder
    void guard_scratch(std::function<void()> then);  // category: helper: save/discard/cancel, THEN do the thing
    void relocate_to(const std::string& target); // category: helper: flush, move the folder, reopen there
    void report_problem(const std::string& summary,
                        const std::string& detail);  // category: helper: say it on screen, not only in the log
    void open_store();                           // category: helper: construct the NodeSource -- THE swap point
    void open_jots(const std::string& dir);     // category: helper: point the surfaces at a jots folder
    void update_note_actions();                  // category: helper: grey what the selection can't do
    // s045: run a model verb as one undoable step (core::Journal::run).
    bool undoable(const std::string& label, const std::vector<core::NodeId>& ids, bool subtree,
                  const core::Journal::Op& op);      // category: helper
    void update_undo_actions();
    void sync_editor_body();                     // category: helper: s046b, the editor re-reads a body an undo changed                  // category: helper: Undo / Redo say what they would do
    void keys_to_tree(const core::NodeId& id);   // category: helper: s046 -- the tree takes the keyboard after the rebuild
    bool focus_is_text() const;                  // category: helper: s046 -- the keyboard is in a text box / entry
    std::string prefs_file() const;              // category: helper: the XDG path for the layout pump
    core::AttachStore& ingest_store();           // category: helper: where a new enclosure goes (folder or scratch)
    const core::AttachStore* attach_store() const;  // category: helper: the same, read-only, for the drawer
    int retarget_everywhere(const std::string& from, const std::string& to);  // category: helper: relink rewrites every note
    bool current_note_editable() const;          // category: helper: s020 -- a convert edits the note; refuse before copying
    int retarget_current(const std::string& from, const std::string& to);  // category: helper: s020 convert rewrites THIS note
    void convert_to_link(const std::string& name, const std::string& abs_path);  // category: helper: s020 Link Instead's second half
    void place_enclosures(const std::vector<std::pair<std::string, std::string>>& added,
                          int offset);           // category: helper: record metadata, write the references in
    std::string pending_dir() const;             // category: helper: the XDG path for the capture spool
    void drain_pending();                        // category: helper: file what was captured while jot was closed
    void apply_layout_state();                   // category: helper: ONE writer for both panes' visibility
    void queue_drawer_refresh();                 // category: helper: coalesce index + drawer to one idle
    void queue_tree_rebuild();                   // category: helper: rebuild the tree on an idle, never inside a gesture
    void queue_inbox_refresh();                  // category: helper: s028 Inbox pane + tab count + Clean Up greying, on an idle
    void queue_projects_refresh();               // category: helper: s037 the Projects pane, debounced, only while it is showing
    void queue_tags_refresh();                   // category: helper: s035 the Tags pane, debounced, only while it is showing
    void remember_window_geometry();             // category: helper: size + maximized -> prefs, at close
    void refresh_tasks(bool rebuild_index, const core::NodeId& id = {});  // category: helper: ONE writer for the task index + Today
    void queue_desktop_sync();                   // category: helper: arm the debounce; the only way a sync starts
    void run_desktop_sync(bool force);           // category: helper: the sync itself, past the fingerprint
    void on_desktop_result(const DesktopResult& r);  // category: helper: one landing place for every desktop result
    void start_notify_timer();                   // category: helper: the minute clock behind due notifications
    void check_due_notifications();              // category: helper: announce what has just come due
    void on_notify_receipt(const Receipt& r);    // category: helper: what the daemon said back
    void send_unreceipted(const Notice& n);      // category: helper: the road with no receipt on it
    void commit_announced(const std::string& key); // category: helper: a deadline is said ONCE it has landed
    void show_notify_status();                   // category: helper: the footer's fourth line
    void show_background_status();               // category: helper: the footer's sixth line
    void apply_background_hold();                // category: helper: ONE writer for the application hold
    core::Lifecycle lifecycle_state() const;     // category: helper: what core::on_close/on_quit get to see
    void finish_quit();                          // category: helper: past the prompt -- flush, release, go
    void show_desktop_status(const std::string& s);  // category: helper: the footer's second line

    // Titlebar: brand logo + new-note (start), hamburger (end).
    widgets::Button     m_logo_button;   // frameless brand mark; opens About
    widgets::Button     m_new_button;
    widgets::MenuButton m_menu_button;
    widgets::MenuButton m_note_menu_button;   // s016c: the note verbs, beside the main menu

    // The capture line. Always visible rather than summoned: a capture box you
    // have to open first costs a click, and the whole premise is that capture
    // costs nothing.
    widgets::Entry      m_capture;
    // True when the placeholder is currently saying what was just captured, so
    // the next keystroke can put it back. A confirmation that never clears is
    // a label; one that clears on a timer is a race with how fast you type.
    bool m_capture_tell = false;

    // The header TITLE is a menu button (GNOME's own idiom -- Text Editor,
    // Builder). s004 put path.filename() here, which for the default default
    // was the literal string "jots": the least informative component of the
    // path, and the same word every jots folder would show. Now the folder
    // name sits over the folder it is in, and the menu carries the absolute
    // path plus the three things you actually want to do with a location.
    widgets::MenuButton m_jots_button;
    widgets::Label      m_jots_name;     // the folder
    widgets::Label      m_jots_where;    // the folder it's in
    Glib::RefPtr<Gio::Menu> m_jots_menu; // rebuilt in place; its section LABEL is the path

    // Body: tree | note | drawer, as NESTED Paned -- paned_left holds the tree
    // and paned_right, and paned_right holds the note and the drawer. Lifted
    // from Folio's sidebar|centre|inspector, which is already jot's shape.
    //
    // The per-child flags are the non-obvious part and they are not symmetric:
    // the NOTE absorbs every resize and the two side panes hold their widths,
    // because the note is the thing you are looking at.
    // The left pane is no longer "the tree": it is HOW YOU NAVIGATE, with two
    // ways of looking at the same nodes stacked behind one switcher. Three
    // panes still hold; nothing moved.
    widgets::Box                m_left;
    widgets::Box                m_left_tabs;
    widgets::ToggleButton       m_tab_notes;
    widgets::ToggleButton       m_tab_inbox;     // s028
    Gtk::Label*                 m_tab_inbox_count = nullptr;   // s048: the number beside the tray
    widgets::ToggleButton       m_tab_today;
    widgets::ToggleButton       m_tab_tags;      // s035
    widgets::ToggleButton       m_tab_projects;  // s037
    // s038: the Notes page is the Find field over a Stack of tree | results.
    widgets::Box                m_notes_page;
    widgets::Box                m_find_row;     // s038b: the field and its filter menu, one row
    widgets::SearchEntry        m_find;
    widgets::MenuButton         m_find_menu;    // s038b
    widgets::Stack              m_notes_stack;
    widgets::Stack              m_left_stack;

    widgets::Paned              m_paned_left;
    widgets::Paned              m_paned_right;
    std::unique_ptr<TreePane>   m_tree;
    std::unique_ptr<EditorPane> m_editor;
    std::unique_ptr<DrawerPane> m_drawer;
    std::unique_ptr<TodayPane>  m_today;
    std::unique_ptr<InboxPane>  m_inbox;      // s028
    std::unique_ptr<TagsPane>   m_tags;       // s035
    std::unique_ptr<ProjectsPane> m_projects; // s037
    std::unique_ptr<SearchPane> m_search;     // s038

    // The two toggles. BOTH OFF is the focus mode -- the note alone on screen,
    // which is the front door ARCHITECTURE describes. They are held so
    // apply_layout_state() can set them without re-entering their own handlers.
    widgets::ToggleButton m_tree_toggle;
    widgets::ToggleButton m_drawer_toggle;
    // s034: Source | Live | Reading, one joined control where the eye was.
    // Three ToggleButtons on ONE stateful string action (win.view-mode), the
    // way the Notes | Inbox | Today tabs are -- pressed says which view is up.
    widgets::ToggleButton m_mode_source;
    widgets::ToggleButton m_mode_live;
    widgets::ToggleButton m_mode_reading;
    bool m_applying_layout = false;

    // Layout state, persisted. A pane you can hide has to come back the way you
    // left it, or hiding it is something you do once and then re-do at every
    // launch.
    core::Prefs m_prefs;
    std::string m_prefs_file;

    // Who points at whom. Rebuilt in one pass whenever the store changes
    // wholesale, updated incrementally when a body changes. The Shell owns it
    // because it is the only thing that sees every write.
    core::LinkIndex m_links;
    bool m_drawer_refresh_queued = false;
    bool m_tree_rebuild_queued   = false;
    bool m_inbox_refresh_queued  = false;   // s028
    sigc::connection m_projects_refresh;     // s037
    sigc::connection m_search_refresh;       // s038
    sigc::connection m_tags_refresh;         // s035: the debounce; tags live in bodies, so keystrokes count

    // Which nodes are todos, in document order. Owned here for the same reason
    // the link index is: the Shell is the only thing that sees every write.
    // It holds MEMBERSHIP only -- every availability verdict is re-derived by
    // core on each query, so there is no cached answer here to go stale.
    core::TaskIndex m_tasks;

    // ── the desktop projection (s009, rewritten s010) ──────────────────────────────────────
    // jot owns the truth; the EDS CALENDAR is a projection wiped and rewritten
    // from it. Held here for the same reason the indexes are: the Shell is the
    // only thing that sees every write.
    //
    // THE DEBOUNCE IS NOT A POLISH ITEM. refresh_tasks() runs on every model
    // change, and a sync is a synchronous D-Bus round trip into another
    // process; wired straight through, ticking a box would stall the frame that
    // drew the tick. So a change ARMS a timer, the timer compares the
    // projection's fingerprint against the last one written, and only a real
    // difference crosses the boundary.
    //
    // m_desktop_sig is a fingerprint of what WE LAST SENT, not a cache of the
    // model: the projection is recomputed from the model every time it is
    // consulted. If the list is changed behind jot's back the next real change
    // rewrites it wholesale anyway, because the sync is always a full rebuild.
    //
    // ── and the connect is ASYNC (s010) ────────────────────────────────────
    // The first connect to EDS costs up to thirty seconds and that is measured,
    // not feared, so sync() can come back PENDING: the work is queued behind a
    // connect and a second result arrives later through the report callback.
    // m_desktop_inflight_* is what that later result needs and cannot carry --
    // the fingerprint it was hoping to record, and how many rows it set out
    // with. Committed on success, dropped on failure, never guessed at.
    Desktop           m_desktop;
    std::string       m_desktop_sig;
    std::string       m_desktop_inflight_sig;
    std::size_t       m_desktop_inflight_count = 0;
    sigc::connection  m_desktop_timer;

    // ── due notifications (s011) ───────────────────────────────────────────
    // A projection is state and a notification is an EVENT, so this half needs
    // a CLOCK -- there is no model change when a deadline simply arrives. Sixty
    // seconds is chosen against what a due date means: they are dates, or times
    // to the minute, and nothing in jot can express a deadline finer than that.
    //
    // m_notify_ok is whether the installed .desktop entry was found. A
    // notification is routed by the daemon through the application id, and one
    // it cannot look up is DROPPED SILENTLY -- so without this check the
    // feature's failure mode is indistinguishable from a code bug, which is
    // exactly the shape the desktop status line exists to prevent.
    sigc::connection  m_notify_timer;
    bool              m_notify_ok = false;

    // ── what the status line could not say, and cost ten minutes (s011) ────
    // "On. 1 due todo announced" counts KEYS, not SENDS: it cannot tell
    // "GNOME showed it" from "GNOME ate it", and it does not say WHEN. The
    // first notification of s011 was a 16:01 deadline announced at 16:05 --
    // at LAUNCH, not at the deadline -- and a timestamp would have said so at
    // a glance instead of costing a debugging session. The desktop line one
    // row up had carried its clock time since s009.
    std::time_t m_notify_last_at = 0;      // when the last ANSWER came back
    std::string m_notify_last_title;       // and what it was about

    // ── s015: a send is not a delivery ─────────────────────────────────────
    // The status line used to time the SEND, which is the moment jot stopped
    // knowing anything. These three are the answer instead: what happened to
    // the last one, whether anybody confirmed it, and how many the daemon has
    // taken from this process. A count of confirmations is the first number on
    // that line that could not have been produced by a notification nobody saw.
    Notifier     m_notifier{"io.github.scott8420.Jot"};
    core::Outbox m_outbox;                 // asked, not yet answered
    core::Journal m_journal;               // s045: the navigator's undo; cleared on a folder swap
    // s046b: THE door. Every surface but the note body writes through this, so
    // every model write is an undo step. Asks for m_store at each call.
    core::UndoSource m_undo{m_journal, [this] { return m_store.get(); }};
    std::size_t  m_notify_delivered = 0;   // receipts, this run
    bool         m_notify_verified  = false;  // ... was the last one one of them?
    std::string  m_notify_last_error;      // the daemon's own words, if it refused
    std::size_t  m_notify_live_n    = 0;   // deadlines currently standing
    std::string  m_notify_last_act;        // s041: what the last button press did

    // ── residency (s012) ───────────────────────────────────────────────────
    // m_holding mirrors the application hold so release() is never called
    // without a matching hold: GApplication counts them, and one release too
    // many takes the count negative and quits a process somebody is using.
    bool m_holding  = false;
    bool m_quitting = false;   // a quit is under way; the close handler must let go

    // THE SWAP. s002 held a MemoryNodes full of fixtures here; s003 holds a
    // Project reading a folder. The type below is the INTERFACE -- m_project is the
    // concrete handle, non-null whenever the store is a real jots folder, and it
    // exists only for the two things a jots folder can do that a NodeSource can't:
    // flush to disk and name its own directory.
    std::unique_ptr<core::NodeSource> m_store;
    core::Project*                      m_project = nullptr;

    // The scratch buffer's enclosures (s016b). The notes are in memory, but an
    // image cannot be: it is staged in <data>/jot/scratch-attachments/ the
    // moment it arrives, so nothing dropped is lost, and adopt MOVES what the
    // notes reference into the new folder's attachments/. The metadata lives
    // here, in memory, beside the notes it describes -- lost or kept with them.
    core::AttachStore                   m_scratch_attach;

    // Recents -- recent JOTS FOLDERS now that D1 is answered, which is the job
    // the folder pump was kept alive for.
    std::vector<std::string>        m_recents;
    std::string                     m_recents_file;
    Glib::RefPtr<Gio::Menu>         m_recents_menu;   // live submenu, rebuilt in place
    Glib::RefPtr<Gio::SimpleAction> m_recents_clear;  // held to toggle enabled state

    // Note actions held so the selection can grey them (nothing selected, or a
    // protected node, means delete and new-child are not offers).
    Glib::RefPtr<Gio::SimpleAction> m_act_new_child;
    Glib::RefPtr<Gio::SimpleAction> m_act_delete;
    Glib::RefPtr<Gio::SimpleAction> m_act_protect;
    Glib::RefPtr<Gio::SimpleAction> m_act_rename_note;

    // Todo verbs. Held for the same greying reason -- and the two that only
    // make sense on something that IS a todo grey themselves on a plain note,
    // rather than silently doing nothing.
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_todo;
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_done;
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_flag;
    // s028. Clean Up greys itself when nothing is processed; In Inbox is a
    // check item ticked from the model, like Protected.
    Glib::RefPtr<Gio::SimpleAction> m_act_clean_up;
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_inbox;
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_project;   // s037b, bool: the selection is a project
    // s029. "Move to..." -- greyed on no selection or a protected note, the
    // two cases where the picker would have nothing to offer.
    Glib::RefPtr<Gio::SimpleAction> m_act_move_to;
    Glib::RefPtr<Gio::SimpleAction> m_act_undo_nav, m_act_redo_nav;               // s045
    Glib::RefPtr<Gio::SimpleAction> m_act_indent, m_act_outdent, m_act_move_up, m_act_move_down;
    Glib::RefPtr<Gio::SimpleAction> m_act_mark_reviewed;   // s037: greyed unless the selection is a project
    Glib::RefPtr<Gio::SimpleAction> m_act_project_state;   // s031, radio: the selection's project state

    // Which half of the left pane is showing. A stateful STRING action, so the
    // two tab buttons and the View menu's radio items draw from one place --
    // the same shape the two pane toggles use.
    Glib::RefPtr<Gio::SimpleAction> m_act_left_view;
    // s038b: the filter menu's state is READ from the field's text by
    // sync_find_actions(), never kept -- the text is the filter.
    Glib::RefPtr<Gio::SimpleAction> m_act_find_show;
    Glib::RefPtr<Gio::SimpleAction> m_act_find_due;
    Glib::RefPtr<Gio::SimpleAction> m_act_find_flagged;
    Glib::RefPtr<Gio::SimpleAction> m_act_find_est;      // s040
    Glib::RefPtr<Gio::SimpleAction> m_act_find_delete;
    Glib::RefPtr<Gio::SimpleAction> m_act_find_save;
    Glib::RefPtr<Gio::Menu>         m_find_model;

    // Location actions: held because none of them is an offer when there is no
    // jots folder open (JOT_STRESS, or a first run still being answered).
    Glib::RefPtr<Gio::SimpleAction> m_act_open_in_files;
    Glib::RefPtr<Gio::SimpleAction> m_act_copy_path;
    Glib::RefPtr<Gio::SimpleAction> m_act_relocate;
    Glib::RefPtr<Gio::SimpleAction> m_act_rename;

    // The view toggles, held as STATEFUL actions so the header button and the
    // menu item draw from one place and cannot disagree about what is showing.
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_tree;
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_drawer;
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_reading;   // s021
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_live;      // s022
    Glib::RefPtr<Gio::SimpleAction> m_act_view_mode;        // s034: "source" | "live" | "reading"
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_desktop;
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_notify;
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_background;

    // The About window: a hide-on-close singleton, built lazily on first open.
    std::unique_ptr<AboutWindow>     m_about;
    std::unique_ptr<CheatSheetWindow> m_cheat_sheet;   // s030: core::cheat_sheet()'s GTK consumer (same lifetime stone)
    std::unique_ptr<ShortcutsDialog> m_shortcuts;   // shortcut registry's GTK consumer (same lifetime stone)
    // The preferences window (s014). Same hide-on-close singleton stone, and the
    // first home the three desktop toggles have had that is not the Today
    // footer. Built lazily: most sessions never open it.
    std::unique_ptr<PreferencesWindow> m_preferences;

    // The naming dialog. One instance, rebuilt per occasion because its mode
    // is fixed at construction and there are only three of them.
    std::unique_ptr<JotsFolderDialog> m_name_dialog;

    // s029. The Move picker, rebuilt per use (the note it moves is fixed at
    // construction). Its Recent list lives in m_prefs.move_recent, per jots
    // folder; the scratch buffer has no folder and keeps none.
    std::unique_ptr<MoveDialog> m_move_dialog;
    std::unique_ptr<PerspectiveDialog> m_persp_dialog;   // s038b

    // Set only by guard_scratch's continuation, so the second close_request
    // (the one that actually closes) doesn't ask again. Nothing else may
    // set it -- a force-close flag that anything can raise is a data-loss bug
    // waiting for a careless edit.
    bool m_force_close = false;

    // JOT_STRESS: the store is a synthetic set, not a scratch buffer. Without
    // this, closing the D6 instrument would offer to save 5,000 fake notes --
    // the scratch machinery is right and the question is nonsense.
    bool m_stress = false;
};

}  // namespace jot
