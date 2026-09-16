#pragma once
#include "widgets/Widgets.hpp"

#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/grid.h>
#include <gtkmm/window.h>

#include <sigc++/signal.h>
#include <string>

namespace jot {

// ─────────────────────────────────────────────────────────────────────────────
// PreferencesWindow -- the window jot has been putting off since s009.
//
// Three settings had already been pushed into the TODAY FOOTER because there
// was nowhere else to put them, which is a pane about today's tasks carrying
// the app's configuration. The capture hotkey is what finally forces the
// question: a chord is not a check box, it needs a row, a label, a conflict
// warning and a command line you can read, and none of that fits in a footer.
//
// The footer's boxes STAY. They and the rows here drive the SAME win.* stateful
// actions -- one piece of state, three consumers (menu item, footer box,
// preferences row), which is the shape jot has used for view state since s006.
// Two widgets setting a bool is how the menu ends up ticked and the box does
// not.
//
// Hide-on-close singleton, like AboutWindow and ShortcutsDialog: built once by
// the Shell and re-presented, so its named children never re-register.
//
// THE HOTKEY ROW IS THE ONLY THING HERE THAT IS NOT JOT'S OWN STATE. It reads
// and writes GNOME's keybinding settings (see Keybinding.hpp), which is why it
// refreshes on every present() rather than caching: the user can change or
// delete that shortcut in GNOME Settings while this window is closed, and a
// preferences row that keeps insisting otherwise is worse than no row.
// ─────────────────────────────────────────────────────────────────────────────
class PreferencesWindow : public Gtk::Window {
public:
    PreferencesWindow();
    ~PreferencesWindow() override;

    void show(Gtk::Window& parent);

    // Set WITHOUT re-emitting -- the Shell is the writer, exactly as with the
    // Today footer's boxes.
    void set_desktop_on(bool on);
    void set_notify_on(bool on);
    void set_background_on(bool on);

    sigc::signal<void(bool)>& signal_desktop_toggled()    { return m_sig_desktop; }
    sigc::signal<void(bool)>& signal_notify_toggled()     { return m_sig_notify; }
    sigc::signal<void(bool)>& signal_background_toggled() { return m_sig_background; }

private:
    int  build_hotkey_section(int row);
    int  build_running_section(int row);

    // Read GNOME's settings and repaint the row from what is ACTUALLY there.
    void refresh_hotkey();
    void begin_grab();                 // "press a combination" mode
    void end_grab();                   // back to the resting row
    bool on_key(guint keyval, guint keycode, Gdk::ModifierType state);
    void apply_accel(const std::string& accel);
    void say(const std::string& text, bool problem);

    int  add_heading(const std::string& title, int row);
    int  add_note(const std::string& text, int row);
    int  add_row(const std::string& caption, Gtk::Widget& content, int row);

    widgets::Box  m_root{"prefs.root", Gtk::Orientation::VERTICAL};
    Gtk::Grid     m_grid;
    widgets::Button m_btn_close{"prefs.close", "Close"};

    // ── the hotkey row ─────────────────────────────────────────────────────
    widgets::Label  m_hotkey_value{"prefs.hotkey.value"};
    widgets::Button m_hotkey_set{"prefs.hotkey.set", "Set\u2026"};
    widgets::Button m_hotkey_clear{"prefs.hotkey.clear", "Clear"};
    widgets::Label  m_hotkey_status{"prefs.hotkey.status"};
    widgets::Label  m_hotkey_command{"prefs.hotkey.command"};

    // ── the three that used to live only in the footer ─────────────────────
    widgets::CheckButton m_desktop_check{"prefs.desktop_check"};
    widgets::CheckButton m_notify_check{"prefs.notify_check"};
    widgets::CheckButton m_background_check{"prefs.background_check"};

    sigc::signal<void(bool)> m_sig_desktop;
    sigc::signal<void(bool)> m_sig_notify;
    sigc::signal<void(bool)> m_sig_background;

    Glib::RefPtr<Gtk::EventControllerKey> m_keys;
    bool m_grabbing  = false;   // the window is swallowing keys, waiting for a chord
    bool m_setting   = false;   // a set_*_on() is in flight; don't re-emit
    bool m_supported = false;   // GNOME's keybinding schemas are installed
};

}  // namespace jot
