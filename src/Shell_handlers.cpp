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

#include <gtkmm/filedialog.h>
#include <giomm/appinfo.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>
#include <glibmm/miscutils.h>

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
                m_drawer->show_node("");
            } else {
                m_drawer->refresh();   // a backlink may have just died
            }
            break;
        case C::Task:
            // A task edit can change what is AVAILABLE somewhere else entirely
            // -- ticking a step makes its sequential sibling the next action,
            // three rows down and possibly under a collapsed parent. So the
            // whole report is re-derived rather than the one row repainted.
            refresh_tasks(false, id);
            if (id == m_editor->current()) m_drawer->refresh();
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
            m_drawer->refresh();
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
        // is what keeps the menu item, the Today footer box and the
        // preferences row in step without any of the three knowing the others
        // exist -- the same wiring the footer got in s009 and s012.
        m_preferences->signal_desktop_toggled().connect(
            [this](bool) { activate_action("win.toggle-desktop"); });
        m_preferences->signal_notify_toggled().connect(
            [this](bool) { activate_action("win.toggle-notify"); });
        m_preferences->signal_background_toggled().connect(
            [this](bool) { activate_action("win.toggle-background"); });
    }
    m_preferences->set_desktop_on(m_prefs.desktop_tasks);
    m_preferences->set_notify_on(m_prefs.notify_due);
    m_preferences->set_background_on(m_prefs.background);
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
    if (m_today) m_today->set_desktop_on(m_prefs.desktop_tasks);
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
    if (m_today) m_today->set_notify_on(m_prefs.notify_due);
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
    if (m_today) m_today->set_background_on(m_prefs.background);
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

}  // namespace jot
