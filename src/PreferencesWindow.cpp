#include "PreferencesWindow.hpp"

#include "Keybinding.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Hotkey.hpp"
#include "core/Shortcuts.hpp"

#include <gtkmm/accelerator.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>

#include <gdk/gdkkeysyms.h>

namespace jot {
namespace {

// A press of Control alone is not a chord, it is the first half of one. Without
// this the grab would end the instant a modifier went down and record nothing.
bool modifier_key(guint kv) {
    switch (kv) {
        case GDK_KEY_Control_L: case GDK_KEY_Control_R:
        case GDK_KEY_Shift_L:   case GDK_KEY_Shift_R:
        case GDK_KEY_Alt_L:     case GDK_KEY_Alt_R:
        case GDK_KEY_Super_L:   case GDK_KEY_Super_R:
        case GDK_KEY_Meta_L:    case GDK_KEY_Meta_R:
        case GDK_KEY_Hyper_L:   case GDK_KEY_Hyper_R:
        case GDK_KEY_Caps_Lock: case GDK_KEY_Num_Lock:
        case GDK_KEY_ISO_Level3_Shift:
            return true;
        default:
            return false;
    }
}

}  // namespace

PreferencesWindow::PreferencesWindow() {
    set_name("shell.preferences");
    registry::add("shell.preferences", this);

    set_title("jot Preferences");
    set_modal(false);
    set_resizable(true);
    set_default_size(600, 560);
    set_hide_on_close(true);   // built once by the Shell, re-presented after that

    m_grid.set_margin(16);
    m_grid.set_column_spacing(12);
    m_grid.set_row_spacing(4);
    m_grid.set_hexpand(true);

    int r = 0;
    r = add_heading("Capture", r);
    r = build_hotkey_section(r);
    // s016a: one heading per FEATURE, where s014 had one heading for all three
    // switches. They used to be three boxes in the Today footer, titled by
    // their own labels; here each gets the room to say what it does, which is
    // what the footer's tooltips were quietly doing instead.
    r = build_running_section(r);

    m_btn_close.set_halign(Gtk::Align::END);
    m_btn_close.set_margin(8);
    m_btn_close.signal_clicked().connect([this]() { set_visible(false); });

    // Scrolls, because it will grow: s016a took it from two headings to four,
    // and a preferences window that clips its last section on a small screen
    // is one whose last setting nobody finds.
    m_grid.set_vexpand(true);
    m_scroll.set_child(m_grid);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    m_scroll.set_propagate_natural_height(true);
    m_root.append(m_scroll);
    m_root.append(m_btn_close);
    set_child(m_root);

    // ── the key grab ────────────────────────────────────────────────────────
    // CAPTURE phase, on the window, so a chord reaches this before any focused
    // widget gets a say: without it, Ctrl+N inside a preferences window is
    // still Ctrl+N and jot would make a note while the user was trying to name
    // a shortcut. The controller is always attached; m_grabbing is what decides
    // whether it swallows anything.
    m_keys = Gtk::EventControllerKey::create();
    m_keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    m_keys->signal_key_pressed().connect(
        sigc::mem_fun(*this, &PreferencesWindow::on_key), false);
    add_controller(m_keys);
}

PreferencesWindow::~PreferencesWindow() { registry::remove(this); }

void PreferencesWindow::show(Gtk::Window& parent) {
    set_transient_for(parent);
    // ALWAYS re-read, never cache. The shortcut lives in GNOME's settings, and
    // the user can change or delete it in GNOME Settings while this window is
    // closed. A row that insists on what it saw last time is a row that lies.
    end_grab();
    refresh_hotkey();
    present();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sections
// ─────────────────────────────────────────────────────────────────────────────
int PreferencesWindow::build_hotkey_section(int row) {
    m_hotkey_value.set_halign(Gtk::Align::START);
    m_hotkey_value.set_hexpand(true);
    m_hotkey_value.set_xalign(0.0f);
    m_hotkey_value.add_css_class("monospace");

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    box->append(m_hotkey_value);
    box->append(m_hotkey_set);
    box->append(m_hotkey_clear);

    m_hotkey_set.signal_clicked().connect([this]() {
        if (m_grabbing) end_grab();
        else            begin_grab();
    });
    m_hotkey_clear.signal_clicked().connect([this]() {
        end_grab();
        std::string err;
        if (keys::uninstall(&err)) say("Cleared. The shortcut is gone from GNOME.", false);
        else                       say(err, true);
        refresh_hotkey();
    });

    row = add_row("Global capture shortcut", *box, row);

    m_hotkey_status.set_halign(Gtk::Align::START);
    m_hotkey_status.set_xalign(0.0f);
    m_hotkey_status.set_wrap(true);
    m_grid.attach(m_hotkey_status, 1, row++, 1, 1);

    // The command line is SHOWN, not hidden. It names this exact binary, so a
    // jot that was rebuilt somewhere else has a shortcut pointing at the old
    // path -- and the only way to notice is to be able to read it.
    m_hotkey_command.set_halign(Gtk::Align::START);
    m_hotkey_command.set_xalign(0.0f);
    m_hotkey_command.set_wrap(true);
    m_hotkey_command.set_selectable(true);
    m_hotkey_command.add_css_class("monospace");
    m_hotkey_command.add_css_class("dim-label");
    m_grid.attach(m_hotkey_command, 1, row++, 1, 1);

    row = add_note("The shortcut opens jot with the cursor in the capture line. "
                   "It works when jot isn't running \u2014 GNOME starts it.", row);
    return row;
}

int PreferencesWindow::build_running_section(int row) {
    m_desktop_check.set_label("Show dated todos in the GNOME calendar");
    m_notify_check.set_label("Notify me when a todo is due");
    m_background_check.set_label("Keep jot running when the window is closed");

    // The signal, not the state: the ACTION is the writer, and these boxes ask
    // it to flip. m_setting is what keeps set_*_on() from looping back out.
    m_desktop_check.signal_toggled().connect([this]() {
        if (!m_setting) m_sig_desktop.emit(m_desktop_check.get_active());
    });
    m_notify_check.signal_toggled().connect([this]() {
        if (!m_setting) m_sig_notify.emit(m_notify_check.get_active());
    });
    m_background_check.signal_toggled().connect([this]() {
        if (!m_setting) m_sig_background.emit(m_background_check.get_active());
    });

    // Notifications first: ON by default, and the one most people will touch.
    row = add_heading("Notifications", row);
    m_grid.attach(m_notify_check, 1, row++, 1, 1);
    row = add_note("A desktop notification when an available todo reaches its "
                   "due date; click it to open the todo here. Blocked and "
                   "deferred todos stay quiet. What was last delivered is "
                   "reported at the foot of Today.", row);

    row = add_heading("Desktop calendar", row);
    m_grid.attach(m_desktop_check, 1, row++, 1, 1);
    row = add_note("Dated todos appear as all-day events in the calendar "
                   "drop-down. One way only: nothing done there comes back "
                   "here. This writes into another application's calendar, "
                   "which is why it is off until you turn it on.", row);
    m_desktop_note.set_halign(Gtk::Align::START);
    m_desktop_note.set_xalign(0.0f);
    m_desktop_note.set_wrap(true);
    m_desktop_note.add_css_class("dim-label");
    m_desktop_note.set_text("This build of jot has no desktop calendar support.");
    m_desktop_note.set_visible(false);
    m_grid.attach(m_desktop_note, 1, row++, 1, 1);

    // Last, because it is the precondition the other two lean on rather than a
    // feature of its own. The note says what the X will DO, and no longer
    // promises a Background Apps entry: s012 looked, and an unsandboxed jot is
    // not listed there.
    row = add_heading("When the window closes", row);
    m_grid.attach(m_background_check, 1, row++, 1, 1);
    row = add_note("Closing the window hides it instead of quitting, so due "
                   "dates are still announced. Quit from the menu, or Ctrl+Q, "
                   "really quits.", row);
    return row;
}

void PreferencesWindow::set_desktop_available(bool can) {
    m_desktop_check.set_sensitive(can);
    m_desktop_note.set_visible(!can);
}

void PreferencesWindow::set_desktop_on(bool on) {
    m_setting = true;
    m_desktop_check.set_active(on);
    m_setting = false;
}
void PreferencesWindow::set_notify_on(bool on) {
    m_setting = true;
    m_notify_check.set_active(on);
    m_setting = false;
}
void PreferencesWindow::set_background_on(bool on) {
    m_setting = true;
    m_background_check.set_active(on);
    m_setting = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// The hotkey row
// ─────────────────────────────────────────────────────────────────────────────
void PreferencesWindow::refresh_hotkey() {
    const keys::Status st = keys::current();
    m_supported = st.supported;

    if (!st.supported) {
        m_hotkey_value.set_text("Not available");
        m_hotkey_command.set_text("");
        m_hotkey_set.set_sensitive(false);
        m_hotkey_clear.set_sensitive(false);
        say("A global shortcut is set through GNOME's keyboard settings, and "
            "this desktop doesn't have them. jot --capture still works from a "
            "terminal or your own keybinding.", false);
        return;
    }

    m_hotkey_set.set_sensitive(true);
    m_hotkey_clear.set_sensitive(st.installed);

    if (!st.installed) {
        m_hotkey_value.set_text("None");
        m_hotkey_command.set_text("");
        if (m_hotkey_status.get_text().empty())
            say("Not set. Press Set\u2026 and then the combination you want.", false);
        return;
    }

    m_hotkey_value.set_text(core::format_accel(st.accel));
    m_hotkey_command.set_text(st.command);
    if (!st.ours) {
        // Somebody else is in jot's slot, or the command was hand-edited into
        // something jot does not recognise. Say so rather than overwriting it
        // silently on the next Set.
        say("That shortcut is in jot's slot but doesn't look like jot's own "
            "command. Setting it again will replace it.", true);
    } else {
        say("Set. GNOME runs this command when you press it.", false);
    }
}

void PreferencesWindow::begin_grab() {
    if (!m_supported) return;
    m_grabbing = true;
    m_hotkey_set.set_label("Cancel");
    m_hotkey_value.set_text("Press a combination\u2026");
    say("Esc cancels. Backspace clears the shortcut.", false);
}

void PreferencesWindow::end_grab() {
    m_grabbing = false;
    m_hotkey_set.set_label("Set\u2026");
}

bool PreferencesWindow::on_key(guint keyval, guint, Gdk::ModifierType state) {
    if (!m_grabbing) return false;          // not our turn -- the window behaves normally
    if (modifier_key(keyval)) return true;  // swallowed, still waiting for the chord

    if (keyval == GDK_KEY_Escape) {
        end_grab();
        refresh_hotkey();
        return true;
    }
    if (keyval == GDK_KEY_BackSpace) {
        end_grab();
        std::string err;
        if (keys::uninstall(&err)) say("Cleared. The shortcut is gone from GNOME.", false);
        else                       say(err, true);
        refresh_hotkey();
        return true;
    }

    const auto mods = state & Gtk::Accelerator::get_default_mod_mask();
    const std::string accel = core::canonical_accel(
        Gtk::Accelerator::name(keyval, mods).raw());
    end_grab();
    apply_accel(accel);
    return true;
}

void PreferencesWindow::apply_accel(const std::string& accel) {
    // core decides whether the chord is fit to be GLOBAL; this only reports.
    const std::string objection = core::hotkey_objection(accel);
    if (!objection.empty()) {
        refresh_hotkey();
        say(objection, true);
        return;
    }

    std::string err;
    if (!keys::install(accel, core::capture_command(keys::exe_path()), &err)) {
        refresh_hotkey();
        say(err, true);
        return;
    }
    refresh_hotkey();

    // The conflict scan is ADVISORY and runs AFTER the write, deliberately: it
    // cannot see extensions or an application's own grab, so refusing on it
    // would be refusing on evidence jot knows is incomplete. The shortcut is
    // set; the warning says what else claims the chord, and the user decides.
    const auto clash = keys::conflicts(accel);
    if (clash.empty()) return;
    std::string msg = "Set \u2014 but the desktop already uses that combination for ";
    for (std::size_t i = 0; i < clash.size() && i < 3; ++i) {
        if (i) msg += ", ";
        msg += clash[i].what + " (" + clash[i].where + ")";
    }
    msg += ". Whichever wins, the other one stops working \u2014 pick another if "
           "that one matters.";
    say(msg, true);
    if (auto lg = log::get(log::Area::App))
        lg->warn("capture hotkey {} collides with {} existing binding(s)", accel,
                 clash.size());
}

void PreferencesWindow::say(const std::string& text, bool problem) {
    m_hotkey_status.set_text(text);
    m_hotkey_status.remove_css_class("error");
    m_hotkey_status.remove_css_class("dim-label");
    m_hotkey_status.add_css_class(problem ? "error" : "dim-label");
}

// ─────────────────────────────────────────────────────────────────────────────
// Grid helpers -- each returns the next free row.
// ─────────────────────────────────────────────────────────────────────────────
int PreferencesWindow::add_heading(const std::string& title, int row) {
    auto* lbl = Gtk::make_managed<Gtk::Label>(title);
    lbl->set_halign(Gtk::Align::START);
    lbl->set_margin_top(row == 0 ? 0 : 18);
    lbl->add_css_class("heading");
    m_grid.attach(*lbl, 0, row, 2, 1);
    auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep->set_margin_bottom(6);
    m_grid.attach(*sep, 0, row + 1, 2, 1);
    return row + 2;
}

int PreferencesWindow::add_row(const std::string& caption, Gtk::Widget& content,
                               int row) {
    auto* lbl = Gtk::make_managed<Gtk::Label>(caption);
    lbl->set_halign(Gtk::Align::START);
    lbl->set_valign(Gtk::Align::CENTER);
    m_grid.attach(*lbl, 0, row);
    content.set_hexpand(true);
    m_grid.attach(content, 1, row);
    return row + 1;
}

int PreferencesWindow::add_note(const std::string& text, int row) {
    auto* lbl = Gtk::make_managed<Gtk::Label>(text);
    lbl->set_halign(Gtk::Align::START);
    lbl->set_xalign(0.0f);
    lbl->set_wrap(true);
    lbl->set_margin_top(4);
    lbl->add_css_class("dim-label");
    m_grid.attach(*lbl, 1, row, 1, 1);
    return row + 1;
}

}  // namespace jot
