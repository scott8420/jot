#pragma once
#include "JotsFolderDialog.hpp"
#include "core/Nodes.hpp"
#include "core/Links.hpp"
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
class PreferencesWindow;
class TreePane;
class EditorPane;
class DrawerPane;
class TodayPane;

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
    void focus_capture_bar();                    // category: helper: put the cursor in the box

    // Reveal and select a node from OUTSIDE the window -- what App dispatches a
    // due notification's click to. A thin public door onto the same
    // reveal-then-select that drawer links already use, rather than a second
    // path that could drift from it.
    void goto_note(const core::NodeId& id) { on_goto_note(id); }

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
    void on_left_view(const Glib::ustring& which);  // category: handler: Notes <-> Today
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
    void on_shortcuts();                         // category: handler: keyboard reference (renders from core::shortcut_registry)
    void on_toggle_tree();                       // category: handler: show/hide the tree
    void on_toggle_drawer();                     // category: handler: show/hide the metadata drawer
    void on_toggle_desktop();                    // category: handler: turn the desktop projection on/off
    void on_toggle_notify();                     // category: handler: turn due notifications on/off
    void on_toggle_background();                 // category: handler: stay running with no window, or don't
    void on_desktop_sync_now();                  // category: handler: rewrite the desktop list now
    void on_goto_note(const core::NodeId& id);   // category: handler: a drawer link row -> reveal + select
    void on_copy_link(const core::NodeId& id);   // category: handler: [Title](jot:<id>) -> clipboard
    void on_images_dropped(std::vector<std::string> paths, int offset);  // category: handler: files dropped on the body -> enclosures
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
    std::string prefs_file() const;              // category: helper: the XDG path for the layout pump
    core::AttachStore& ingest_store();           // category: helper: where a new enclosure goes (folder or scratch)
    const core::AttachStore* attach_store() const;  // category: helper: the same, read-only, for the drawer
    void place_enclosures(const std::vector<std::pair<std::string, std::string>>& added,
                          int offset);           // category: helper: record metadata, write the references in
    std::string pending_dir() const;             // category: helper: the XDG path for the capture spool
    void drain_pending();                        // category: helper: file what was captured while jot was closed
    void apply_layout_state();                   // category: helper: ONE writer for both panes' visibility
    void queue_drawer_refresh();                 // category: helper: coalesce index + drawer to one idle
    void queue_tree_rebuild();                   // category: helper: rebuild the tree on an idle, never inside a gesture
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
    widgets::ToggleButton       m_tab_today;
    widgets::Stack              m_left_stack;

    widgets::Paned              m_paned_left;
    widgets::Paned              m_paned_right;
    std::unique_ptr<TreePane>   m_tree;
    std::unique_ptr<EditorPane> m_editor;
    std::unique_ptr<DrawerPane> m_drawer;
    std::unique_ptr<TodayPane>  m_today;

    // The two toggles. BOTH OFF is the focus mode -- the note alone on screen,
    // which is the front door ARCHITECTURE describes. They are held so
    // apply_layout_state() can set them without re-entering their own handlers.
    widgets::ToggleButton m_tree_toggle;
    widgets::ToggleButton m_drawer_toggle;
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
    std::size_t  m_notify_delivered = 0;   // receipts, this run
    bool         m_notify_verified  = false;  // ... was the last one one of them?
    std::string  m_notify_last_error;      // the daemon's own words, if it refused
    std::size_t  m_notify_live_n    = 0;   // deadlines currently standing

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

    // Which half of the left pane is showing. A stateful STRING action, so the
    // two tab buttons and the View menu's radio items draw from one place --
    // the same shape the two pane toggles use.
    Glib::RefPtr<Gio::SimpleAction> m_act_left_view;

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
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_desktop;
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_notify;
    Glib::RefPtr<Gio::SimpleAction> m_act_toggle_background;

    // The About window: a hide-on-close singleton, built lazily on first open.
    std::unique_ptr<AboutWindow>     m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;   // shortcut registry's GTK consumer (same lifetime stone)
    // The preferences window (s014). Same hide-on-close singleton stone, and the
    // first home the three desktop toggles have had that is not the Today
    // footer. Built lazily: most sessions never open it.
    std::unique_ptr<PreferencesWindow> m_preferences;

    // The naming dialog. One instance, rebuilt per occasion because its mode
    // is fixed at construction and there are only three of them.
    std::unique_ptr<JotsFolderDialog> m_name_dialog;

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
