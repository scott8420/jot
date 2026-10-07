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

    // ── s062e: a jot older than its own binary ──────────────────────────────
    // jot is single-instance and stays resident, so `./build/jot` after a
    // rebuild only handed over to the OLD process (Scott: "really hard to know
    // that an older Jot is running"). stale() is true when the program file
    // this process was started from has been replaced since: /proc/self/exe
    // reads "... (deleted)", or the path now names a different file.
    bool stale() const;
    // Quit (asking what needs asking), then main() execs the new binary in
    // this same process -- the bus name is released by then, so the new jot
    // comes up as the one instance. false when there is no new file to run.
    bool restart_new_build();
    const std::string& restart_path() const { return m_restart; }

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
    void on_notice(const Glib::VariantBase& target);   // s041: a notification button

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
    // s025c: `list` -- `jot --list NAME item...`; `words` keeps each argument
    // whole (an item may be several words in quotes), `text` joins them.
    struct Request {
        bool capture = false;
        bool list = false;
        bool append = false;   // s025d
        bool both = false;     // s061e: --both NAME "line" items (core/Cli writes it)
        std::string text;
        std::vector<std::string> words;
    };
    static Request parse(const std::vector<std::string>& argv, bool capture_flag,
                         bool list_flag = false, bool append_flag = false,
                         bool both_flag = false);

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
    bool        file_pending(const std::string& text, const std::string& list = {},
                             const std::string& append = {},
                             const std::string& uniq_suffix = {}) const;

    Shell* m_shell = nullptr;   // owned by the application via add_window

    // s062e: the program file as it was at startup.
    std::string        m_exe;
    unsigned long long m_exe_ino = 0;
    long long          m_exe_mtime = 0;
    std::string        m_restart;   // set: main() execs this once run() returns
};

}  // namespace jot
