#pragma once
#include <string>
#include <vector>
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
// s016a finished the move: the footer's boxes are GONE and this window is the
// one place a setting lives (Scott's call). The rows drive win.* stateful
// actions rather than owning a bool -- one piece of state, with the menu item
// and the row as its consumers -- which is the shape jot has used for view
// state since s006. The footer keeps the STATUS lines, because a report of what
// another process did has to be where you look, and a setting does not.
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

    // Set WITHOUT re-emitting -- the Shell is the writer.
    void set_desktop_available(bool can);   // false: box greyed, and a line says why
    void set_desktop_on(bool on);
    void set_notify_on(bool on);
    void set_background_on(bool on);
    void set_drop_links_on(bool on);
    // s050b: the highlight colour -- "" (System) or "#rrggbb" -- and what the
    // desktop's own accent is, for the line under the swatches.
    void set_accent(const std::string& chosen, const std::string& desktop);

    sigc::signal<void(bool)>& signal_desktop_toggled()    { return m_sig_desktop; }
    sigc::signal<void(bool)>& signal_notify_toggled()     { return m_sig_notify; }
    sigc::signal<void(bool)>& signal_background_toggled() { return m_sig_background; }
    sigc::signal<void(bool)>& signal_drop_links_toggled() { return m_sig_drop_links; }
    sigc::signal<void(std::string)>& signal_accent_chosen() { return m_sig_accent; }   // s050b

    // s070: Keyboard -- how many shortcuts are changed; Edit opens the
    // Keyboard Shortcuts window editing; Reset All asks once, then signals.
    void set_keys_changed(int n);
    sigc::signal<void()>& signal_edit_keys()  { return m_sig_edit_keys; }
    sigc::signal<void()>& signal_reset_keys() { return m_sig_reset_keys; }

    // s075: Backups. The Shell owns the state and the work; the rows ask.
    void set_backup(bool on, const std::string& where, bool is_default,
                    const std::string& status, bool busy, bool has_folder);
    sigc::signal<void(bool)>& signal_backup_toggled() { return m_sig_backup_on; }
    sigc::signal<void()>& signal_backup_choose()  { return m_sig_backup_choose; }
    sigc::signal<void()>& signal_backup_reset()   { return m_sig_backup_reset; }
    sigc::signal<void()>& signal_backup_now()     { return m_sig_backup_now; }
    sigc::signal<void()>& signal_backup_restore() { return m_sig_backup_restore; }

private:
    int  build_hotkey_section(int row);
    int  build_running_section(int row);
    int  build_enclosure_section(int row);
    int  build_look_section(int row);          // s050b
    int  build_keyboard_section(int row);      // s070
    int  build_backup_section(int row);        // s075
    void choose_accent(const std::string& hex);   // a swatch or the picker -> the Shell
    void show_accent();                           // swatches + the line, from m_accent

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
    widgets::ScrolledWindow m_scroll{"prefs.scroll"};
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
    widgets::Label       m_desktop_note{"prefs.desktop_note"};
    widgets::CheckButton m_drop_links_check{"prefs.drop_links_check"};

    sigc::signal<void(bool)> m_sig_desktop;
    sigc::signal<void(bool)> m_sig_notify;
    sigc::signal<void(bool)> m_sig_background;
    sigc::signal<void(bool)> m_sig_drop_links;

    // ── s050b: the highlight colour ────────────────────────────────────────
    // One control: System, GNOME's nine, then any colour. Exclusive by hand
    // (not a GTK toggle group) because a custom colour leaves NO swatch
    // pressed, which a group cannot do.
    struct Swatch { std::string hex; widgets::ToggleButton* button = nullptr; };
    std::vector<Swatch>        m_swatches;   // [0] is System ("")
    widgets::ColorDialogButton* m_custom = nullptr;
    widgets::Label             m_accent_says{"prefs.accent.says"};
    std::string                m_accent;            // "" or "#rrggbb"
    std::string                m_desktop_accent;
    sigc::signal<void(std::string)> m_sig_accent;

    // ── s070: Keyboard ─────────────────────────────────────────────────────
    widgets::Label  m_keys_says{"prefs.keys.says"};
    widgets::Button m_keys_edit{"prefs.keys.edit", "Edit Shortcuts\u2026"};
    widgets::Button m_keys_reset{"prefs.keys.reset", "Reset All to Defaults"};
    int             m_keys_changed = 0;
    sigc::signal<void()> m_sig_edit_keys, m_sig_reset_keys;

    // ── s075: Backups ──────────────────────────────────────────────────────
    widgets::CheckButton m_backup_check{"prefs.backup.check"};
    widgets::Label  m_backup_where{"prefs.backup.where"};
    widgets::Button m_backup_choose{"prefs.backup.choose", "Choose\u2026"};
    widgets::Button m_backup_reset{"prefs.backup.reset", "Default"};
    widgets::Label  m_backup_says{"prefs.backup.says"};
    widgets::Button m_backup_now{"prefs.backup.now", "Back Up Now"};
    widgets::Button m_backup_restore{"prefs.backup.restore", "Restore\u2026"};
    sigc::signal<void(bool)> m_sig_backup_on;
    sigc::signal<void()> m_sig_backup_choose, m_sig_backup_reset, m_sig_backup_now,
                         m_sig_backup_restore;

    Glib::RefPtr<Gtk::EventControllerKey> m_keys;
    bool m_grabbing  = false;   // the window is swallowing keys, waiting for a chord
    bool m_setting   = false;   // a set_*_on() is in flight; don't re-emit
    bool m_supported = false;   // GNOME's keybinding schemas are installed
};

}  // namespace jot
