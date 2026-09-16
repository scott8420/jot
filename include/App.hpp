#pragma once
#include <giomm/applicationcommandline.h>
#include <gtkmm/application.h>

#include <memory>
#include <string>
#include <vector>

// App -- the Gtk::Application. Thin: stands up logging in create(), builds the
// Shell on activate. (CANON: thin bootstrap -- main -> App::create() -> Shell.)
namespace jot {

class Shell;

class App : public Gtk::Application {
public:
    static Glib::RefPtr<App> create();

protected:
    App();
    void on_activate() override;
    void on_startup() override;   // accelerators, driven from core::shortcut_registry()

    // ── `jot --capture` ─────────────────────────────────────────────────────
    // HANDLES_COMMAND_LINE plus GTK's default single-instance behaviour is the
    // whole mechanism: a second `jot` forwards its argv to the running one over
    // the session bus and exits. There is NO DAEMON -- no autostart, no portal,
    // no second lifecycle.
    //
    // s013 fills in the other half. With jot NOT running, `jot --capture
    // "milk"` used to LAUNCH THE APP -- a window, a tree, a jots folder opened,
    // all so a line could be written down. Now the thought goes into the
    // pending spool and the process exits without ever making a window, and the
    // next launch drains it. `cmd->is_remote()` is the whole test: false means
    // this process is the primary instance, which means there was nothing
    // running to forward to.
    int on_command_line(const Glib::RefPtr<Gio::ApplicationCommandLine>& cmd) override;

    // ── `app.goto-node` -- what a due notification's click lands on ─────────
    // On the APPLICATION, not the window, and that is the whole reason it is
    // here rather than next to Shell's other actions. A notification outlives
    // the window that sent it: GNOME keeps it in the message tray and the click
    // may arrive after jot has been closed entirely. The daemon activates the
    // application BY ID and dispatches into its action group, so a `win.` action
    // would have nothing to land in -- and a dropped activation is a return
    // value nobody reads, which is exactly how s009's unprefixed
    // `toggle-desktop` managed to do nothing in silence.
    void on_goto_node(const Glib::VariantBase& target);

    // ── `app.quit` -- what GNOME's Background Apps list presses ────────────
    // The Quit item next to a background app activates this over D-Bus. An
    // application with no "quit" action does not get asked politely; it gets
    // SIGKILLed, which for jot would mean an unsaved scratch buffer dying
    // silently. It forwards to the Shell, which is the only object that knows
    // whether anything is at risk.
    void on_quit();

private:
    // argv -> what to do. Pure, so the parsing is a thing the selftest could
    // reach if it ever needs to, and so on_command_line stays about plumbing.
    struct Request { bool capture = false; std::string text; };
    static Request parse(const std::vector<std::string>& argv, bool capture_flag);

    // ── ensure_shell -- activation, with the presenting made a DECISION ─────
    // on_activate() presented unconditionally, which was invisible until s012:
    // "already running" used to mean the window was on screen anyway. A
    // resident jot has no window, so `jot --capture "milk"` arriving over the
    // bus yanked the whole app onto the screen -- exactly what that path's own
    // comment says must not happen.
    //
    // So the two halves are separated. Everything that has to be true before a
    // capture can land (a Shell exists; it is attached to this application)
    // happens either way; present() happens only when someone ASKED to be
    // looked at. on_command_line calls this directly rather than activate(),
    // which it can because on_command_line always runs in the PRIMARY instance.
    void ensure_shell(bool present);

    // ── the cold-capture spool ─────────────────────────────────────────────
    // Where a capture goes when there is no running jot to hand it to. The XDG
    // resolution is the UI layer's job (core takes a plain path), and the
    // subpath itself is core::pending_dir so this and the Shell's drain cannot
    // disagree about the folder.
    std::string pending_dir() const;
    bool        file_pending(const std::string& text) const;

    Shell* m_shell = nullptr;   // owned by the application via add_window
};

}  // namespace jot
