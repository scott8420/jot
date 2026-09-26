#include "Shell.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "PreferencesWindow.hpp"
#include "DrawerPane.hpp"
#include "EditorPane.hpp"
#include "TreePane.hpp"
#include "TodayPane.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Recents.hpp"
#include "core/Import.hpp"

#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>
#include <giomm/liststore.h>
#include <gdkmm/clipboard.h>
#include <gdkmm/texture.h>
#include <gtkmm/filelauncher.h>
#include <gtkmm/urilauncher.h>
#include <giomm/appinfo.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>
#include <glibmm/miscutils.h>

#include <ctime>
#include <filesystem>

// Shell_handlers.cpp -- HANDLERS. Slot bodies: what a user action actually does.
// Each is small and single-concern; orchestration it shares lives in helpers.

namespace jot {

// ── notes ───────────────────────────────────────────────────────────────────
// Every one of these is a model call and nothing else. The tree and the editor
// find out the same way an outside change would tell them: on_model_changed.

void Shell::on_new_note() {  // handler: new top-level note
    const auto id = m_store->create("", "");
    if (!id.empty()) m_tree->select(id);
    on_selection_changed(id);
    m_editor->focus_capture();
}

void Shell::on_new_child() {  // handler: new note under the selection
    const auto parent = m_tree->selected();
    if (parent.empty()) return on_new_note();
    const auto id = m_store->create(parent, "");
    if (!id.empty()) m_tree->select(id);
    on_selection_changed(id);
    m_editor->focus_capture();
}

void Shell::on_delete_note() {  // handler: delete the selected subtree
    const auto id = m_tree->selected();
    if (id.empty()) return;
    if (!m_store->remove(id))
        if (auto lg = log::get(log::Area::Model))
            lg->info("delete '{}': refused (protected, here or below)", id);
}

void Shell::on_toggle_protect() {  // handler: lock/unlock the selection
    const auto id = m_tree->selected();
    const core::Node* n = id.empty() ? nullptr : m_store->find(id);
    if (n) m_store->set_protect(id, !n->protect);
}

// Enter in the capture line. The box keeps the focus afterwards, because the
// thought you just captured is very often followed by another one -- and
// because moving the cursor somewhere else would be the app deciding you were
// finished.
void Shell::on_capture_activate() {  // handler: Enter in the capture line
    const std::string text = std::string(m_capture.get_text());
    if (text.find_first_not_of(" \t") == std::string::npos) return;
    capture(text);
    m_capture.set_text("");
}

// ── the three todo verbs ────────────────────────────────────────────────────
// Each is two lines because the model already knows how to do the work and the
// drawer already knows how to show it. They exist so that a todo is reachable
// from the keyboard and from a right-click, not only from a pane that can be
// hidden -- ticking something off should never require opening a panel.
void Shell::on_toggle_todo() {  // handler: make the selection a todo (or not)
    const auto id = m_tree->selected();
    const core::Node* n = id.empty() ? nullptr : m_store->find(id);
    if (n) m_store->make_task(id, !n->task.is_task);
}

void Shell::on_toggle_done() {  // handler: tick/untick the selection
    const auto id = m_tree->selected();
    const core::Node* n = id.empty() ? nullptr : m_store->find(id);
    if (n && n->task.is_task) m_store->set_done(id, !n->task.done);
}

void Shell::on_toggle_flag() {  // handler: flag/unflag the selection
    const auto id = m_tree->selected();
    const core::Node* n = id.empty() ? nullptr : m_store->find(id);
    if (n && n->task.is_task) m_store->set_flagged(id, !n->task.flagged);
}

// F2 and the context menu's Rename both land here. The tree owns what an inline
// rename IS; this only says which row -- and it switches to the Notes view
// first, because renaming a row that is not on screen would look like nothing
// happening at all.
void Shell::on_rename_note() {  // handler: rename in the tree
    const auto id = m_tree->selected();
    if (id.empty()) return;
    if (m_act_left_view) m_act_left_view->change_state(Glib::ustring("notes"));
    m_tree->begin_rename(id);
}

// Notes <-> Today. The action holds the state; this is the one writer of what
// the stack shows, which is why the tab buttons and the menu items cannot
// disagree about which view is up.
void Shell::on_left_view(const Glib::ustring& which) {  // handler: Notes <-> Today
    if (m_act_left_view) m_act_left_view->set_state(Glib::Variant<Glib::ustring>::create(which));
    m_left_stack.set_visible_child(which == "today" ? "today" : "notes");
    if (which == "today") m_today->refresh();   // it may have been away a while
}

void Shell::on_selection_changed(const core::NodeId& id) {  // handler: tree row -> editor
    // Leaving a note is a natural moment to land its deferred body write: the
    // one you just left is the one you would most regret losing.
    if (m_project && m_project->dirty()) m_project->flush();
    m_editor->show_node(id);
    // The note being left may have had its links edited, and the index is only
    // as current as the last idle. Land it before moving on, or the drawer on
    // the new note shows a backlink the old note no longer declares.
    if (!m_editor->current().empty()) m_links.update(*m_store, m_editor->current());
    m_drawer->show_node(id);
    update_note_actions();
}

// The one place the surfaces learn anything. The Change kind decides what
// repaints: a body edit changes no row, so it must not rebuild the tree -- at
// 5000 nodes that would be a full repaint per keystroke.
void Shell::on_model_changed(core::NodeSource::Change what, const core::NodeId& id) {
    using C = core::NodeSource::Change;
    // QUEUED, not immediate. A model change can arrive from inside a click on
    // a tree row -- the drawer's date entries commit on focus-leave -- and a
    // synchronous rebuild would destroy the row the gesture is still using.
    // See queue_tree_rebuild().
    if (what != C::Body) queue_tree_rebuild();

    switch (what) {
        case C::Reload:
            m_links.rebuild(*m_store);
            refresh_tasks(true);
            m_tree->select("");
            m_editor->show_node("");
            m_drawer->show_node("");
            break;
        case C::Removed:
            // A removal takes a SUBTREE, so any number of ids are gone and the
            // one we were handed is only the root of the cut. Rebuilding the
            // index is O(bodies) and a delete is rare; walking the vanished
            // subtree to erase each one would need the subtree, which is
            // precisely what no longer exists to be walked.
            m_links.rebuild(*m_store);
            refresh_tasks(true);          // the cut subtree may have held todos
            if (!m_editor->current().empty() && !m_store->find(m_editor->current())) {
                m_tree->select("");
                m_editor->show_node("");
            }
            // The drawer on an IDLE in both arms (s017 fix): its rows hold live
            // controllers now -- a menu button with an open popover, a
            // double-click gesture -- and a rebuild from inside an event being
            // delivered to one of them is the gtk_widget_get_parent CRITICAL.
            // An idle refresh of a vanished note shows the empty drawer.
            queue_drawer_refresh();   // a backlink may have just died, too
            break;
        case C::Task:
            // A task edit can change what is AVAILABLE somewhere else entirely
            // -- ticking a step makes its sequential sibling the next action,
            // three rows down and possibly under a collapsed parent. So the
            // whole report is re-derived rather than the one row repainted.
            refresh_tasks(false, id);
            if (id == m_editor->current()) queue_drawer_refresh();   // idle: see Removed
            break;
        case C::Moved:
            // A move changes DOCUMENT ORDER, and document order is what decides
            // which child of a sequential parent is the next action. Dragging a
            // step up the list is how you prioritise in jot -- so a drag has to
            // reach the report, and this is the line that makes it.
            refresh_tasks(true);
            [[fallthrough]];
        case C::Flags:
            // Protection makes the editor read-only, and the drawer carries the
            // parent and the protected state -- both are invalidated by a change
            // neither of them authored. (This is what the status line used to do.)
            if (id == m_editor->current()) m_editor->refresh();
            queue_drawer_refresh();   // idle: see Removed
            break;
        case C::Title: {
            // TWO surfaces can rename: the tree's inline rename and the
            // drawer's Name field. Neither talks to the other -- both write
            // through the model and both hear about it here, which is the only
            // arrangement in which adding a THIRD rename surface later costs
            // nothing. (There WERE three; the editor's title entry was cut in
            // s006, and cutting it touched nothing but its own file because of
            // exactly this.)
            //
            // The drawer is told the new title rather than asked to reload, and
            // ignores it while its field has focus -- focus is how a pane knows
            // it was the author.
            const core::Node* n = m_store->find(id);
            if (n && id == m_editor->current()) m_drawer->title_changed_elsewhere(n->title);
            // And separately: a title change ANYWHERE can change what a link
            // row says, because the drawer shows a target's current title
            // rather than the label the link was written with.
            queue_drawer_refresh();
            break;
        }
        case C::Body:
            // Every keystroke lands here. Coalesced to one idle, exactly as the
            // editor coalesces its restyle -- rescanning a body per keystroke to
            // maintain an index is a typing-latency cost for a panel that only
            // has to be right by the time you look at it.
            queue_drawer_refresh();
            break;
        case C::Created:
            break;   // a new note has no links, no backlinks, and is not a todo
    }
    update_note_actions();
}

// ── diagnostics ─────────────────────────────────────────────────────────────

void Shell::on_dump_nodes() {  // handler: print the model tree
    // registry::dump()'s sibling, one layer down: ids and parents rather than
    // widget names. This is what you want the first time a link resolves to
    // nothing, and it is also how you check that a drag moved rather than copied.
    if (auto lg = log::get(log::Area::Model)) lg->info("\n{}", core::dump(*m_store));
}

void Shell::on_dump_registry() {  // handler: print the live widget tree
    // The debuggability payoff, on demand. Same call works from a gdb prompt.
    registry::dump();
}

// ── jots ────────────────────────────────────────────────────────────────────

// New jots: a NAME, not a folder chooser. The user says what these jots are
// called and jot makes `<name>.jots` -- the suffix is never typed and the
// preview shows the whole path before anything is created.
//
// If there are unsaved notes on screen, this IS the save: adopting them into
// the folder being made is obviously better than creating it empty and
// dropping them. The dialog says so in its own words.
void Shell::on_new_jots() {  // handler: name a new jots folder
    const bool adopting = scratch_has_content();
    name_jots(adopting ? JotsFolderDialog::Mode::Save : JotsFolderDialog::Mode::New,
              default_jots_location(), "",
              [this, adopting](const std::string& target) {
                  if (adopting) save_scratch(target);
                  else          open_jots(target);
              });
}

// Rename: a move that stays put. Same core verb as relocate -- the folder is
// renamed within its own parent -- and safe for the same reason: nothing
// inside holds a path, so nothing inside is read.
void Shell::on_rename_jots() {  // handler: rename in place
    if (!m_project) return;
    const std::filesystem::path here(m_project->dir());
    name_jots(JotsFolderDialog::Mode::Rename, here.parent_path().string(),
              core::jots_display_name(m_project->dir()),
              [this](const std::string& target) { relocate_to(target); });
}

void Shell::on_save_all() {  // handler: land every deferred write now
    if (!m_project) return;
    const bool had_work = m_project->dirty();
    const bool ok = m_project->flush();
    if (auto lg = log::get(log::Area::Io))
        lg->info("save all: {}", !had_work ? "nothing outstanding" : (ok ? "written" : "FAILED"));
}

void Shell::on_open_jots() {  // handler: folder chooser -> open that jots folder
    // Leaving the scratch buffer is leaving it, whether by closing the window
    // or by opening something else. One guard, every exit.
    guard_scratch([this]() { open_jots_chooser(); });
}

void Shell::open_jots_chooser() {
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Open or create a jots folder");
    dialog->select_folder(*this,
        [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto dir = dialog->select_folder_finish(result);
                if (!dir) return;
                const std::string folder = dir->get_path();
                // An empty folder becomes a jots folder; a jots folder is opened.
                // There is no separate "new jots folder" verb, because there is no
                // difference between the two on disk.
                if (!folder.empty()) open_jots(folder);
            } catch (const Glib::Error& e) {
                if (auto lg = log::get(log::Area::Shell))
                    lg->info("open-jots: dialog dismissed ({})", e.what());
            }
        });
}

// ── where the jots live ─────────────────────────────────────────────────────
// Three verbs on one location. Each is short because the work is elsewhere:
// the path resolution is in helpers, the move itself is in core where it can
// be tested, and the state of these actions is greyed by update_jots_title().

void Shell::on_open_in_files() {  // handler: show the jots folder in the file manager
    if (!m_project) return;
    // Gio rather than Gtk::FileLauncher: launching a URI is a GIO concern, it
    // has been stable for a decade, and it does not tie this call to a GTK
    // version floor that Scott's Fedora and the sandbox would have to agree on.
    try {
        Gio::AppInfo::launch_default_for_uri(
            Gio::File::create_for_path(m_project->dir())->get_uri());
    } catch (const Glib::Error& e) {
        report_problem("Could not open the folder",
                       std::string(e.what()) + "\n\n" + m_project->dir());
    }
}

void Shell::on_copy_path() {  // handler: jots folder path -> clipboard
    if (!m_project) return;
    get_clipboard()->set_text(m_project->dir());
    if (auto lg = log::get(log::Area::Shell)) lg->info("copied path '{}'", m_project->dir());
}

void Shell::on_relocate_jots() {  // handler: choose a destination, move the whole folder
    if (!m_project) return;
    const std::filesystem::path here(m_project->dir());

    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Move your jots \u2014 choose where the folder should go");
    // The chooser picks the DESTINATION PARENT and the folder keeps its name.
    // Picking the new folder itself would make "relocate" and "rename" one
    // gesture, and a rename that silently moves is how you end up with two
    // jots folders and no memory of which is which.
    std::error_code ec;
    if (std::filesystem::is_directory(here.parent_path(), ec))
        dialog->set_initial_folder(Gio::File::create_for_path(here.parent_path().string()));

    dialog->select_folder(*this,
        [this, dialog, here](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto dir = dialog->select_folder_finish(result);
                if (!dir) return;
                const std::string parent = dir->get_path();
                if (parent.empty()) return;
                relocate_to((std::filesystem::path(parent) / here.filename()).string());
            } catch (const Glib::Error& e) {
                if (auto lg = log::get(log::Area::Shell))
                    lg->info("relocate: dialog dismissed ({})", e.what());
            }
        });
}

void Shell::on_recent_open(const std::string& path) {  // handler: open a recent jots folder
    if (path.empty()) return;
    if (!std::filesystem::exists(path)) {
        core::recents_remove(m_recents, path);
        core::save_recents(m_recents_file, m_recents);
        rebuild_recents_menu();
        if (auto lg = log::get(log::Area::Recents))
            lg->warn("open '{}': gone -- pruned", path);
        return;
    }
    if (auto lg = log::get(log::Area::Recents)) lg->info("open '{}'", path);
    guard_scratch([this, path]() { open_jots(path); });   // which also moves it to the front
}

void Shell::on_recent_clear() {  // handler: empty the recents list
    m_recents.clear();
    core::save_recents(m_recents_file, m_recents);
    rebuild_recents_menu();
}

// ── windows ─────────────────────────────────────────────────────────────────

void Shell::on_shortcuts() {  // handler: open the keyboard reference
    // Same lifetime stone as About: build once, re-present. The window's CONTENT
    // is not written here -- it renders by walking core::shortcut_registry(),
    // the same list bind_accelerators() wires from, so the two can't drift.
    if (!m_shortcuts) m_shortcuts = std::make_unique<ShortcutsDialog>();
    m_shortcuts->show(*this);
}

void Shell::on_preferences() {  // handler: open the preferences window
    // The same lifetime stone as About and the shortcuts window: built once,
    // re-presented. Built LAZILY because most sessions never open it -- and
    // because the hotkey row reads GNOME's settings on every show(), so there
    // is nothing to gain by having it exist early.
    if (!m_preferences) {
        m_preferences = std::make_unique<PreferencesWindow>();
        // The rows do not own the state; they ASK. Routing through the action
        // is what keeps the menu item and the preferences row in step
        // without either of them knowing the others
        // exist -- the same wiring the footer got in s009 and s012.
        m_preferences->signal_desktop_toggled().connect(
            [this](bool) { activate_action("win.toggle-desktop"); });
        m_preferences->signal_notify_toggled().connect(
            [this](bool) { activate_action("win.toggle-notify"); });
        m_preferences->signal_background_toggled().connect(
            [this](bool) { activate_action("win.toggle-background"); });
        // s019: no menu item mirrors this one, so no action -- the box is
        // the only writer, and the Shell just keeps and saves it.
        m_preferences->signal_drop_links_toggled().connect([this](bool on) {
            m_prefs.drop_links = on;
            core::save_prefs(m_prefs_file, m_prefs);
            if (auto lg = log::get(log::Area::Io))
                lg->info("dropped files will be {}", on ? "linked" : "copied");
        });
    }
    m_preferences->set_desktop_available(Desktop::compiled_in());
    m_preferences->set_desktop_on(m_prefs.desktop_tasks);
    m_preferences->set_notify_on(m_prefs.notify_due);
    m_preferences->set_background_on(m_prefs.background);
    m_preferences->set_drop_links_on(m_prefs.drop_links);
    m_preferences->show(*this);
}

void Shell::on_about() {  // handler: open the About window (dialog-lifetime exemplar)
    // Lazily build once, then re-present. Hide-on-close singleton (CANON:
    // "Lifetime shape is design") -- the X hides it, so one instance lives for
    // the session and its named children never re-register.
    if (!m_about) m_about = std::make_unique<AboutWindow>(*this);
    m_about->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// on_save_as -- write a SECOND jots folder with everything in it, and switch.
//
// One implementation for two situations, because they are the same operation:
// `Project::adopt()` walks any NodeSource and copies the tree into a fresh
// folder. From the scratch buffer it is the close prompt's Save arm, reached
// on purpose rather than on the way out. From an open folder it is a copy --
// and the original is left EXACTLY as it is, which is the promise the dialog
// makes and the reason adopt refuses a destination that already has notes.
//
// The flush first is not optional: adopt reads the MODEL, but the folder we
// are leaving behind has to be complete on disk before we stop writing to it.
// Skipping it would leave the last two seconds of typing only in the copy.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_save_as() {  // handler: a second jots folder, and switch to it
    if (m_project) m_project->flush();

    const auto mode = m_project ? JotsFolderDialog::Mode::SaveAs
                                : JotsFolderDialog::Mode::Save;
    // Offer the current name as the starting point when there is one: "Save as"
    // almost always means a variant of what you have, and retyping the name to
    // add a word to it is the most common thing this dialog is asked to do.
    const std::string suggested =
        m_project ? core::jots_display_name(m_project->dir()) : std::string{};

    name_jots(mode, default_jots_location(), suggested,
              [this](const std::string& target) { save_scratch(target); });
}

// ── the view toggles ────────────────────────────────────────────────────────
// Each writes its own flag and then calls ONE apply_layout_state(). Two writers
// for one layout is how the two panes end up disagreeing about whose turn it is.

void Shell::on_toggle_tree() {  // handler: show/hide the tree
    if (m_applying_layout) return;
    m_prefs.show_tree = !m_prefs.show_tree;
    apply_layout_state();
}

void Shell::on_toggle_drawer() {  // handler: show/hide the metadata drawer
    if (m_applying_layout) return;
    m_prefs.show_drawer = !m_prefs.show_drawer;
    apply_layout_state();
}

// s021 -- Source <-> Reading. One flag, one writer (apply_layout_state), the
// same as the two pane toggles: the header button and the menu item draw from
// the action, the editor is told.
void Shell::on_toggle_reading() {  // handler
    if (m_applying_layout) return;
    m_prefs.reading = !m_prefs.reading;
    apply_layout_state();
}

// s022 -- Live Preview on/off. Independent of Reading: it says what EDITING
// looks like, so it can be flipped while Reading shows and takes effect on the
// way back.
void Shell::on_toggle_live() {  // handler
    if (m_applying_layout) return;
    m_prefs.live_preview = !m_prefs.live_preview;
    apply_layout_state();
}

// A double-click in Reading, or a capture, wants to type. Place the cursor
// first (the editor maps the click back to the source), then let the one
// writer flip the rest.
void Shell::on_edit_requested(int source_cp) {  // handler
    if (!m_prefs.reading) return;
    m_prefs.reading = false;
    m_editor->set_reading(false, source_cp);
    apply_layout_state();
}

// A link clicked in Reading. What a target MEANS is the Shell's: a jot: link
// is a note (revealed in the tree, as a drawer link is), an attachment or a
// file:// is an enclosure (opened the way its row's Open does), anything with
// a scheme goes to the desktop.
void Shell::on_read_link(std::string target) {  // handler
    if (const std::string id = core::link_node_id(target); !id.empty()) {
        if (!m_store->find(id)) {
            report_problem("That note is gone", "The link points at a note that no longer exists.");
            return;
        }
        on_goto_note(id);
        return;
    }
    std::string key = core::attachment_name(target);
    if (key.empty()) key = core::linked_key(target);
    if (!key.empty()) {
        on_enclosure_action("open", key);
        return;
    }
    if (target.find(':') == std::string::npos) {
        report_problem("jot does not know where this goes", target);
        return;
    }
    auto launcher = Gtk::UriLauncher::create(target);
    launcher->launch(*this, [this, launcher, target](Glib::RefPtr<Gio::AsyncResult>& r) {
        try {
            launcher->launch_finish(r);
        } catch (const Glib::Error& e) {
            if (e.matches(GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) return;
            report_problem("Could not open " + target, e.what());
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// on_toggle_desktop -- the projection on or off.
//
// TURNING IT OFF CLEARS THE LIST, immediately and synchronously. It does not
// merely stop updating: a desktop still showing yesterday's todos after you
// untick the box is worse than one showing none, because you cannot tell by
// looking that it is stale. The one place a lie is expensive here is the one
// place the user cannot see jot.
//
// Turning it ON syncs at once and does not wait for the debounce -- the tick is
// a request, and a request that takes effect a second and a half later reads as
// a broken tick.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_toggle_desktop() {  // handler: turn the desktop projection on/off
    m_prefs.desktop_tasks = !m_prefs.desktop_tasks;
    m_act_toggle_desktop->set_state(Glib::Variant<bool>::create(m_prefs.desktop_tasks));
    if (m_preferences) m_preferences->set_desktop_on(m_prefs.desktop_tasks);
    core::save_prefs(m_prefs_file, m_prefs);

    m_desktop_timer.disconnect();
    if (m_prefs.desktop_tasks) {
        run_desktop_sync(/*force=*/true);
    } else {
        // clear() may come back PENDING if the first connect is still in
        // flight. disconnect() knows that and DEFERS the drop rather than
        // cancelling it -- untick during a connect must still get the rows off
        // the desktop, or last session's todos sit there forever.
        const auto r = m_desktop.clear();
        m_desktop.disconnect();
        m_desktop_sig.clear();
        on_desktop_result(r);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// on_toggle_notify -- due notifications on or off.
//
// Turning it OFF does not clear the announced set, and that is deliberate.
// Those keys are "things you have already been told about", not a projection of
// anything; wiping them would mean a user who turns notifications off for an
// afternoon gets re-told about every open deadline the moment they turn them
// back on. Off means quiet, not amnesia.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_toggle_notify() {  // handler: turn due notifications on/off
    m_prefs.notify_due = !m_prefs.notify_due;
    m_act_toggle_notify->set_state(Glib::Variant<bool>::create(m_prefs.notify_due));
    if (m_preferences) m_preferences->set_notify_on(m_prefs.notify_due);
    core::save_prefs(m_prefs_file, m_prefs);

    start_notify_timer();
    show_notify_status();
    show_background_status();   // the residency line says what notify costs without it
    if (m_prefs.notify_due) check_due_notifications();
}

// ─────────────────────────────────────────────────────────────────────────────
// on_toggle_background -- does closing the window quit jot?
//
// The switch changes what the X DOES, which makes it the one preference in jot
// that can surprise someone who never read it. So the footer line under it says
// the consequence in words rather than restating the label, and Quit keeps
// working identically either way.
//
// Turning it OFF while the window is open is the only moment this can be
// reached from -- with no window there is no check box -- so there is no
// "release a hold on a process nobody can see" case to handle. The hold is
// applied and released in exactly one place, and this is one of its two callers.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_toggle_background() {  // handler: stay running with no window, or don't
    m_prefs.background = !m_prefs.background;
    m_act_toggle_background->set_state(Glib::Variant<bool>::create(m_prefs.background));
    if (m_preferences) m_preferences->set_background_on(m_prefs.background);
    core::save_prefs(m_prefs_file, m_prefs);

    apply_background_hold();
    show_background_status();
    show_notify_status();

    if (auto lg = log::get(log::Area::Shell))
        lg->info("background residency {}", m_prefs.background ? "on" : "off");
}

// The manual verb. It FORCES past the fingerprint, because the one occasion you
// reach for "do it now" is when you suspect the desktop and jot disagree -- and
// a fingerprint says they agree by construction. An escape hatch that consults
// the thing you are trying to escape is not one.
void Shell::on_desktop_sync_now() {  // handler: rewrite the desktop list now
    if (!m_prefs.desktop_tasks) {
        show_desktop_status("Tick the box first \u2014 nothing is being written.");
        return;
    }
    m_desktop_timer.disconnect();
    run_desktop_sync(/*force=*/true);
}

// ─────────────────────────────────────────────────────────────────────────────
// on_goto_note -- a drawer link row was activated.
//
// REVEAL, not select. The target of a backlink is very often somewhere you are
// not looking: under a collapsed parent, in a branch you folded away last week.
// TreePane::select() is a no-op on a row that is not currently emitted, so a
// bare select would have made roughly half of all backlink clicks do nothing at
// all -- and do nothing SILENTLY, which is the worst version.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_goto_note(const core::NodeId& id) {  // handler: drawer link -> tree
    if (id.empty() || !m_store->find(id)) return;
    m_tree->reveal(id);
    on_selection_changed(id);
}

// ─────────────────────────────────────────────────────────────────────────────
// on_copy_link -- `[Title](jot:<id>)` onto the clipboard.
//
// This is the answer to "I need to see the uuid" -- which was never the actual
// need. Nobody wants an id; they want a link, and handing over an id to
// transcribe by hand was asking the user to do the one part a machine is better
// at. The id stays folded away in the drawer's Identity disclosure for the
// genuine developer case (correlating with the log and notes/<uuid>.md).
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_copy_link(const core::NodeId& id) {  // handler: link -> clipboard
    const core::Node* n = id.empty() ? nullptr : m_store->find(id);
    if (!n) return;
    const std::string title = n->title.empty() ? std::string("Untitled") : n->title;
    const std::string link = "[" + title + "](" + core::kJotScheme + n->id + ")";
    get_clipboard()->set_text(link);
    if (auto lg = log::get(log::Area::Shell)) lg->info("copied link: {}", link);
}

// ─────────────────────────────────────────────────────────────────────────────
// Files in (s016b images; s018 any file). Copy first, reference second: by the
// time the note says `![..](attachments/x.png)` or `[..](attachments/x.pdf)`,
// the file is already on disk. The other order would
// leave a window in which the note points at nothing.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_files_dropped(std::vector<std::string> paths, int offset, bool flip) {  // handler
    auto& store = ingest_store();
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    // s019: the preference says copy or link; Shift on the drop (a MOVE) says
    // the other one. Decided once for the whole drop.
    const bool link = m_prefs.drop_links != flip;
    if (auto lg = log::get(log::Area::Io))
        lg->info("drop: {} ({}{})", link ? "link" : "copy",
                 m_prefs.drop_links ? "pref link" : "pref copy", flip ? ", flipped" : "");
    std::vector<std::pair<std::string, std::string>> added;
    std::vector<std::string> failed;
    for (const auto& p : paths) {
        std::string err;
        const std::string name = link ? core::link_file(store, p, now, err)
                                      : core::ingest_file(store, p, now, err);
        if (name.empty()) {
            failed.push_back(p + " (" + err + ")");
            continue;
        }
        if (auto lg = log::get(log::Area::Io))
            lg->info("enclosure: '{}' -> {}/{}", p, store.dir, name);
        added.emplace_back(name, core::image_label(p));
    }
    place_enclosures(added, offset);
    if (!failed.empty()) {
        std::string detail;
        for (const auto& f : failed) detail += f + "\n";
        report_problem(failed.size() == 1 ? "A file could not be enclosed"
                                          : "Some files could not be enclosed",
                       detail);
    }
}

void Shell::on_image_pasted(std::string png, int offset) {  // handler
    auto& store = ingest_store();
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    std::string err;
    const std::string name =
        core::ingest_bytes(store, png, core::paste_filename(now), "clipboard", now, err);
    if (name.empty()) {
        report_problem("The pasted image could not be saved", err);
        return;
    }
    if (auto lg = log::get(log::Area::Io))
        lg->info("enclosure: pasted {} byte(s) -> {}/{}", png.size(), store.dir, name);
    place_enclosures({{name, "pasted image"}}, offset);
}

// ─────────────────────────────────────────────────────────────────────────────
// Enclosures out (s017). Every verb resolves the name through
// core::enclosure_path -- the one place a name becomes a path -- and every
// failure is said on screen, because each of these is something the user
// asked for and would otherwise watch do nothing.
//
// Open and Show in Files hand the file to the desktop (GtkFileLauncher, which
// goes through the OpenURI portal when there is one and gio otherwise). Copy
// Image puts a TEXTURE on the clipboard, so it pastes as a picture anywhere
// -- including back into jot, where it becomes a new enclosure. Save a Copy
// asks where, then core::copy_out writes it; the enclosure is never touched.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_enclosure_action(std::string verb, std::string key) {  // handler
    const core::AttachStore* store = attach_store();
    const std::string path = store ? core::enclosure_path(*store, key) : std::string{};
    const bool linked = core::is_linked_key(key);
    // What a message calls it: the attachment name, or a linked file's own name.
    const std::string name = linked ? std::filesystem::path(path).filename().string() : key;

    // s019: Relink is the one verb for a file that is NOT there -- that is
    // what it is for -- so it goes before the presence check.
    if (verb == "relink") {
        if (!linked) return;   // a stale menu; embedded files are not relinked
        auto dialog = Gtk::FileDialog::create();
        dialog->set_title("Relink \u201c" + name + "\u201d");
        // Start where it used to be, if that folder is still there.
        const auto dir = std::filesystem::path(path).parent_path();
        std::error_code ec;
        if (std::filesystem::is_directory(dir, ec))
            dialog->set_initial_folder(Gio::File::create_for_path(dir.string()));
        dialog->open(*this, [this, dialog, key, name](Glib::RefPtr<Gio::AsyncResult>& r) {
            Glib::RefPtr<Gio::File> picked;
            try {
                picked = dialog->open_finish(r);
            } catch (const Glib::Error&) {
                return;   // cancelled
            }
            if (!picked || picked->get_path().empty()) return;
            std::string err;
            const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
            const std::string nk = core::relink(ingest_store(), key, picked->get_path(), now, err);
            if (nk.empty()) {
                report_problem("Could not relink " + name, err);
                return;
            }
            const int n = retarget_everywhere(key, nk);
            if (auto lg = log::get(log::Area::Io))
                lg->info("enclosure relinked: {} -> {} ({} reference(s))", key, nk, n);
        });
        return;
    }
    // s020: CONVERT -- change how this note carries the file.
    if (verb == "embed" || verb == "link") {
        if (!current_note_editable()) {
            report_problem(verb == "embed" ? "Could not embed " + name : "Could not link " + name,
                           "This note is protected, so its references can\u2019t be changed.");
            return;
        }
    }
    if (verb == "embed") {
        if (!linked) return;   // a stale menu
        std::string err;
        const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
        const std::string en = core::embed_copy(ingest_store(), key, now, err);
        if (en.empty()) {
            report_problem("Could not embed a copy of " + name, err);
            return;
        }
        const int n = retarget_current(key, en);
        if (auto lg = log::get(log::Area::Io))
            lg->info("enclosure embedded: {} -> {}/{} ({} reference(s))", key, ingest_store().dir,
                     en, n);
        return;
    }
    if (verb == "link") {
        if (linked) return;   // a stale menu
        const std::string original = store ? core::original_of(*store, key) : std::string{};
        if (original.empty()) {
            report_problem("Could not link " + name, "jot does not know where this file came from.");
            return;
        }
        std::error_code ec;
        if (std::filesystem::is_regular_file(original, ec)) {
            convert_to_link(key, original);
            return;
        }
        // The original moved or went: choose it, starting where it used to be
        // -- Relink's dialog, for the same reason.
        auto dialog = Gtk::FileDialog::create();
        dialog->set_title("Link \u201c" + name + "\u201d to\u2026");
        const auto dir = std::filesystem::path(original).parent_path();
        if (std::filesystem::is_directory(dir, ec))
            dialog->set_initial_folder(Gio::File::create_for_path(dir.string()));
        const core::NodeId note = m_editor->current();
        dialog->open(*this, [this, dialog, key, note](Glib::RefPtr<Gio::AsyncResult>& r) {
            Glib::RefPtr<Gio::File> picked;
            try {
                picked = dialog->open_finish(r);
            } catch (const Glib::Error&) {
                return;   // cancelled
            }
            // The choice was for THAT note; if another is on screen now, the
            // edit would land somewhere nobody asked for.
            if (m_editor->current() != note) return;
            if (picked && !picked->get_path().empty()) convert_to_link(key, picked->get_path());
        });
        return;
    }
    if (verb == "accept") {
        std::string err;
        if (!linked || !core::accept_change(ingest_store(), key, err)) {
            report_problem("Could not accept the change to " + name, err);
            return;
        }
        if (m_project) m_project->enclosures_changed();
        if (auto lg = log::get(log::Area::Io)) lg->info("enclosure change accepted: {}", key);
        queue_drawer_refresh();
        return;
    }

    if (path.empty() || !std::filesystem::is_regular_file(path)) {
        report_problem("That file is not there",
                       linked ? path + " is not there any more. Relink it from the row\u2019s menu."
                              : name + " is not in the attachments folder.");
        return;
    }
    auto file = Gio::File::create_for_path(path);
    if (auto lg = log::get(log::Area::Io)) lg->info("enclosure {}: {}", verb, path);

    if (verb == "open" || verb == "reveal") {
        auto launcher = Gtk::FileLauncher::create(file);
        const bool open = (verb == "open");
        auto done = [this, launcher, open, name](Glib::RefPtr<Gio::AsyncResult>& r) {
            try {
                if (open) launcher->launch_finish(r);
                else      launcher->open_containing_folder_finish(r);
            } catch (const Glib::Error& e) {
                // Dismissing the app chooser is not a failure worth a dialog.
                if (e.matches(GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) return;
                report_problem(open ? "Could not open " + name : "Could not show " + name,
                               e.what());
            }
        };
        if (open) launcher->launch(*this, done);
        else      launcher->open_containing_folder(*this, done);
        return;
    }

    if (verb == "copy") {
        // The menu offers Copy Image only on an image row; this is the guard
        // for a stale menu or a hand-fired action, not the usual road.
        if (!core::is_image_filename(name)) {
            report_problem("Could not copy " + name, "Copy Image is for pictures; use Save a Copy.");
            return;
        }
        try {
            auto tex = Gdk::Texture::create_from_file(file);
            get_clipboard()->set_texture(tex);
        } catch (const Glib::Error& e) {
            report_problem("Could not copy " + name, e.what());
        }
        return;
    }

    if (verb == "save") {
        auto dialog = Gtk::FileDialog::create();
        dialog->set_title("Save a Copy");
        dialog->set_initial_name(name);
        dialog->save(*this, [this, dialog, key, name](Glib::RefPtr<Gio::AsyncResult>& r) {
            Glib::RefPtr<Gio::File> dest;
            try {
                dest = dialog->save_finish(r);
            } catch (const Glib::Error&) {
                return;   // cancelled
            }
            if (!dest || dest->get_path().empty()) return;
            // Re-resolved here, not captured: the store may have been swapped
            // while the dialog was open (Save As), and the name is what the
            // user chose, not the path it had a minute ago.
            const core::AttachStore* now = attach_store();
            std::string err;
            if (!now || !core::copy_out(*now, key, dest->get_path(), err)) {
                report_problem("Could not save a copy of " + name, err);
                return;
            }
            if (auto lg = log::get(log::Area::Io))
                lg->info("enclosure saved: {} -> {}", name, dest->get_path());
        });
        return;
    }
}

// Link Instead's second half (s020): record the link, point this note at it.
// The embedded copy stays in attachments/ -- nothing in the store is deleted,
// and another note may still carry it.
void Shell::convert_to_link(const std::string& name, const std::string& abs_path) {  // helper
    if (!current_note_editable()) return;   // the note changed under an open dialog
    std::string err;
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    const std::string lk = core::link_instead(ingest_store(), name, abs_path, now, err);
    if (lk.empty()) {
        report_problem("Could not link " + name, err);
        return;
    }
    const int n = retarget_current(name, lk);
    if (auto lg = log::get(log::Area::Io))
        lg->info("enclosure linked instead: {} -> {} ({} reference(s))", name, lk, n);
}

// ─────────────────────────────────────────────────────────────────────────────
// Import (s021b). Two roads -- this dialog and a drop on the tree -- and one
// helper, so they cannot disagree about what a file becomes. core::Import
// reads it, titles it and points its pictures home; this makes the notes.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_import_markdown() {  // handler
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Import Markdown");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto md = Gtk::FileFilter::create();
    md->set_name("Markdown and text");
    for (const char* g : {"*.md", "*.MD", "*.markdown", "*.txt"}) md->add_pattern(g);
    filters->append(md);
    auto all = Gtk::FileFilter::create();
    all->set_name("All files");
    all->add_pattern("*");
    filters->append(all);
    dialog->set_filters(filters);
    dialog->open_multiple(*this, [this, dialog](Glib::RefPtr<Gio::AsyncResult>& r) {
        std::vector<Glib::RefPtr<Gio::File>> picked;
        try {
            picked = dialog->open_multiple_finish(r);
        } catch (const Glib::Error&) {
            return;   // cancelled
        }
        std::vector<std::string> paths;
        for (const auto& f : picked)
            if (f && !f->get_path().empty()) paths.push_back(f->get_path());
        import_files(paths, "");   // top level, like New note
    });
}

void Shell::on_import_folder() {  // handler
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Import Markdown Folder");
    dialog->select_folder(*this, [this, dialog](Glib::RefPtr<Gio::AsyncResult>& r) {
        Glib::RefPtr<Gio::File> dir;
        try {
            dir = dialog->select_folder_finish(r);
        } catch (const Glib::Error&) {
            return;   // cancelled
        }
        if (dir && !dir->get_path().empty()) import_files({dir->get_path()}, "");
    });
}

// One file -> one note under `parent`. Empty id (and a line in `failed`) if not.
core::NodeId Shell::import_file(const std::string& p, const core::NodeId& parent,
                                std::vector<std::string>& failed) {  // helper
    const std::string name = std::filesystem::path(p).filename().string();
    if (!core::is_markdown_filename(p)) {
        failed.push_back(name + " (not markdown -- drop it on a note to attach it)");
        return {};
    }
    core::ImportedNote n;
    std::string err;
    if (!core::import_markdown(p, n, err)) {
        failed.push_back(name + " (" + err + ")");
        return {};
    }
    const core::NodeId id = m_store->create(parent, n.title);
    if (id.empty()) {
        failed.push_back(name + " (the note could not be made here)");
        return {};
    }
    m_store->set_body(id, n.body);
    if (auto lg = log::get(log::Area::Io))
        lg->info("imported '{}' -> {} '{}' ({} byte(s), {} picture(s) linked)", p, id, n.title,
                 n.body.size(), n.pictures);
    return id;
}

// A plan -> notes, depth first, in the plan's order. A folder is a parent note
// with an empty body, named after the folder. Returns the item's own note.
core::NodeId Shell::import_item(const core::ImportItem& item, const core::NodeId& parent,
                                std::vector<std::string>& failed, int& made) {  // helper
    if (!item.folder) {
        const core::NodeId id = import_file(item.path, parent, failed);
        if (!id.empty()) ++made;
        return id;
    }
    const core::NodeId id = m_store->create(parent, item.title);
    if (id.empty()) {
        failed.push_back(item.title + "/ (the note could not be made here)");
        return {};
    }
    for (const auto& c : item.children) import_item(c, id, failed, made);
    return id;
}

// Every road in ends here (s021b, s021c): the Files dialog, the Folder dialog,
// and a drop on the tree -- which may hold files AND folders at once.
void Shell::import_files(const std::vector<std::string>& paths, const core::NodeId& parent) {  // helper
    if (paths.empty() || !m_store) return;
    std::vector<std::string> failed;
    core::NodeId last;
    int made = 0;
    for (const auto& p : paths) {
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) {
            core::ImportItem plan;
            bool truncated = false;
            if (!core::plan_folder_import(p, plan, 2000, &truncated)) {
                failed.push_back(std::filesystem::path(p).filename().string() +
                                 "/ (no markdown files in it)");
                continue;
            }
            const int before = made;
            const core::NodeId id = import_item(plan, parent, failed, made);
            if (!id.empty()) last = id;
            if (truncated)
                failed.push_back(plan.title + "/ (stopped at 2000 files -- import the rest "
                                 "folder by folder)");
            if (auto lg = log::get(log::Area::Io))
                lg->info("imported folder '{}' -> {} ({} note(s))", p, id, made - before);
            continue;
        }
        const core::NodeId id = import_file(p, parent, failed);
        if (!id.empty()) { last = id; ++made; }
    }
    // The last thing imported is what you are looking at -- for a folder, its
    // parent note, so the whole import is on screen under it.
    if (!last.empty()) on_goto_note(last);
    if (!failed.empty()) {
        std::string detail;
        for (const auto& f : failed) detail += f + "\n";
        report_problem(failed.size() == 1 ? "Something was not imported"
                                          : "Some things were not imported",
                       detail);
    }
}

}  // namespace jot
