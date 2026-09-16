#include "Shell.hpp"
#include "AboutWindow.hpp"      // complete type for ~unique_ptr<AboutWindow> in ~Shell()
#include "ShortcutsDialog.hpp"  // ditto -- same singleton shape, same dtor requirement
#include "PreferencesWindow.hpp" // ditto (s014)
#include "DrawerPane.hpp"
#include "EditorPane.hpp"
#include "TreePane.hpp"
#include "TodayPane.hpp"
#include "Log.hpp"

#include <giomm/desktopappinfo.h>
#include "core/Prefs.hpp"
#include "core/Recents.hpp"
#include "Registry.hpp"

#include <glibmm/main.h>

#include <cstdlib>
#include <string>

// Shell.cpp -- GLUE. The ctor and the ordered build_ui() pass live here; every
// other method body lives in a category file (see the index in Shell.hpp).

namespace jot {

Shell::Shell()
    : m_logo_button("shell.logo_button"),
      m_new_button("shell.new_button"),
      m_menu_button("shell.menu_button"),
      m_capture("shell.capture"),
      m_jots_button("shell.jots_button"),
      m_jots_name("shell.jots_name"),
      m_jots_where("shell.jots_where"),
      m_left("shell.left", Gtk::Orientation::VERTICAL, 0),
      m_left_tabs("shell.left_tabs", Gtk::Orientation::HORIZONTAL, 0),
      m_tab_notes("shell.tab_notes"),
      m_tab_today("shell.tab_today"),
      m_left_stack("shell.left_stack"),
      m_paned_left("shell.paned_left", Gtk::Orientation::HORIZONTAL),
      m_paned_right("shell.paned_right", Gtk::Orientation::HORIZONTAL),
      m_tree_toggle("shell.tree_toggle"),
      m_drawer_toggle("shell.drawer_toggle") {
    set_name("shell.window");
    m_tree   = std::make_unique<TreePane>("shell.tree");
    m_editor = std::make_unique<EditorPane>("shell.editor");
    m_drawer = std::make_unique<DrawerPane>("shell.drawer");
    m_today  = std::make_unique<TodayPane>("shell.today");
}

Shell::~Shell() = default;

// The ordered pass Scott's Controller/MainWindow shape names: construct the
// shell, bind actions + the model callback, load state, paint. Each step is one
// call into a category file.
void Shell::build_ui() {
    // Prefs BEFORE the shell, because bind_actions() seeds the two stateful
    // toggle actions from them and a stateful action's initial value is set at
    // construction -- reading prefs afterwards would mean the buttons come up
    // showing the opposite of the layout for one frame.
    m_prefs_file = prefs_file();
    m_prefs      = core::load_prefs(m_prefs_file);

    build_shell();     // zone

    // The store must exist before anything binds to its change callback.
    m_recents_file = recents_file();
    m_recents      = core::load_recents(m_recents_file);
    open_store();      // helper -- THE swap point

    bind_actions();    // bindings
    rebuild_recents_menu();

    m_tree->set_source(m_store.get());
    m_editor->set_source(m_store.get());
    m_links.rebuild(*m_store);        // one pass at load -- the backlink index
    m_tasks.rebuild(*m_store);        // and one for the todos
    m_drawer->set_source(m_store.get(), &m_links);
    m_drawer->set_jots_dir(m_project ? m_project->dir() : std::string{});
    m_today->set_source(m_store.get(), &m_tasks);

    // The desktop footer, brought up to match the state the action already
    // holds. `compiled_in()` is a property of the BINARY, so a build without
    // libecal greys the box and says why rather than offering a switch that
    // silently does nothing.
    // Every deferred result lands here. Set BEFORE the first queued sync is
    // armed, or the completion of that sync has nowhere to report to and the
    // status line sits on "Connecting..." forever.
    m_desktop.set_report([this](const DesktopResult& r) { on_desktop_result(r); });

    m_today->set_desktop_available(Desktop::compiled_in());
    m_today->set_desktop_on(m_prefs.desktop_tasks);
    if (!Desktop::compiled_in())
        m_today->set_desktop_status("This build has no desktop support.");
    else if (m_prefs.desktop_tasks) {
        // Last launch left it on, so the list wants rewriting: everything dated
        // "tomorrow" when jot last quit is dated "today" now.
        //
        // But NOT HERE, INLINE. The first connect to Evolution Data Server can
        // take seconds in a cold session (measured: thirty, in a sandbox with
        // no desktop running), and a constructor is the one place in the app
        // where that cost would show as a window that does not appear. So it
        // goes through the ordinary debounce and the window comes up first.
        m_desktop_sig.clear();               // guarantee the queued sync writes
        m_today->set_desktop_status("Checking the desktop\u2026");
        queue_desktop_sync();
    }
    else
        m_today->set_desktop_status(
            "Off. Dated todos stay inside jot.");

    // ── is jot INSTALLED? ──────────────────────────────────────────────────
    // Asked once, here, because the answer cannot change while the process
    // runs and because every notification depends on it. Gio looks the entry up
    // in the XDG application directories exactly as the notification daemon
    // will, so this is the daemon's own question rather than a guess at it.
    m_notify_ok = static_cast<bool>(
        Gio::DesktopAppInfo::create("io.github.scott8420.Jot.desktop"));

    // Every answer the notification service gives lands here. Same shape as the
    // desktop projection's deferred report one field up: the request is made in
    // one place and answered in another, and the state only moves on the answer.
    m_notifier.set_reply([this](const Receipt& r) { on_notify_receipt(r); });
    m_today->set_notify_on(m_prefs.notify_due);
    show_notify_status();
    start_notify_timer();

    // ── residency, and the SAME lookup answering a second question ─────────
    // m_notify_ok is whether GNOME can find jot's .desktop entry. It was asked
    // for notifications; it turns out to be the same prerequisite for being
    // LISTED under Background Apps, because both go through the application id.
    // One lookup, two features, and the status line under the box says so.
    m_today->set_background_on(m_prefs.background);
    show_background_status();
    apply_background_hold();

    // The first check goes on an idle rather than running here: a notification
    // sent from inside the Shell's constructor would be sent before the window
    // it wants to open exists.
    Glib::signal_idle().connect_once([this]() { check_due_notifications(); });

    // ── what was captured while jot was not running ────────────────────────
    // Before the first rebuild, so the drained notes are simply part of the
    // tree that comes up rather than rows that appear a frame later. Does
    // nothing at all unless a jots folder is open -- see drain_pending.
    drain_pending();

    m_tree->rebuild();

    // A scribble pad opens into a note, and that is true with or without a jots
    // folder. With no folder the store is a scratch buffer -- you can type
    // immediately, and the question of where it should live is asked when you
    // leave, which is when it becomes a real question rather than a toll gate.
    auto roots = m_store->children("");
    if (roots.empty()) {
        const auto id = m_store->create("", "");
        if (!id.empty()) roots.push_back(id);
    }
    if (!roots.empty()) {
        m_tree->select(roots.front());
        on_selection_changed(roots.front());
    }
    update_note_actions();
    update_jots_title();   // the header now carries the location; one writer, here
    apply_layout_state();  // and this is the one writer for what is on screen

    // Body edits are deferred rather than skipped: they land on this timer, on
    // a selection change, and on close. Structural changes write immediately --
    // jot.json is small and it is the file whose loss costs the tree.
    Glib::signal_timeout().connect_seconds([this]() {
        if (m_project && m_project->dirty()) m_project->flush();
        return true;
    }, 2);

    // ── Today goes stale on its own, and s012 is where that started to show ─
    // Every other surface repaints because the MODEL moved. Today does not:
    // "Overdue" versus "Due" is a function of the CLOCK, so a jot left open
    // keeps yesterday's labels until you happen to click something. Noticed by
    // Scott watching a row stay "Due" past its deadline.
    //
    // A minute, matching the notification tick, and for the same reason: jot
    // cannot express a deadline finer than that, so nothing on this pane can
    // change sooner. Independent of the notify timer on purpose -- the report
    // must stay honest whether or not you have asked to be interrupted.
    Glib::signal_timeout().connect_seconds([this]() {
        if (m_today) m_today->refresh();
        return true;
    }, 60);
    // Closing is the moment the filing question becomes real. With a folder
    // open there is nothing to ask -- everything is already on disk. Without
    // one, guard_scratch asks, and BLOCKS the close until it is answered; its
    // continuation raises m_force_close and closes again, which is the only
    // path that sets that flag.
    signal_close_request().connect([this]() {
        // First, unconditionally: the geometry is true right now and stops
        // being readable the moment the window starts tearing down. It is
        // recorded even on the paths that do not end in an exit -- the scratch
        // prompt can hold the window open and residency can hide it, and a size
        // learned then is still the right size.
        remember_window_geometry();
        if (m_project) m_project->flush();   // whatever happens next, the disk is current

        // WHAT THE X MEANS is decided in core::on_close, not here. Three
        // answers now, the difference between two of them is whether unsaved
        // notes are about to be lost, and the wrong one is silent -- so the
        // branch lives somewhere a headless test can drive every row of it.
        switch (core::on_close(lifecycle_state())) {
            case core::OnClose::StayResident:
                // Not close(), not hide-and-destroy: VETO the close and drop
                // the surface. The Shell, the store, the scratch buffer and the
                // 60-second timer all stay exactly as they were -- a Glib
                // timeout does not care whether a window is on screen, which is
                // the entire trick. present() brings this same window back.
                set_visible(false);
                if (auto lg = log::get(log::Area::Shell))
                    lg->info("window closed -- staying resident, clock still running");
                return true;

            case core::OnClose::AskFirst:
                guard_scratch([this]() { m_force_close = true; close(); });
                return true;    // hold the window open; the prompt decides

            case core::OnClose::Exit:
                break;
        }
        return false;
    }, false);

    if (auto lg = log::get(log::Area::Shell))
        lg->info("ready -- {} node(s), {} link(s), {} todo(s), {} widget(s) registered",
                 m_store->count(), m_links.edge_count(), m_tasks.count(),
                 registry::size());

}

}  // namespace jot
