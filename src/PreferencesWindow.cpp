#include "PreferencesWindow.hpp"
#include <gtkmm/alertdialog.h>   // s070: Reset All asks once
#include <gtkmm/colordialog.h>
#include <gtkmm/stylecontext.h>
#include <gtkmm/cssprovider.h>
#include "core/RowLook.hpp"

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
    r = build_look_section(r);   // s050b: first -- the one you see before you read
    r = add_heading("Capture", r);
    r = build_hotkey_section(r);
    // s016a: one heading per FEATURE, where s014 had one heading for all three
    // switches. They used to be three boxes in the Today footer, titled by
    // their own labels; here each gets the room to say what it does, which is
    // what the footer's tooltips were quietly doing instead.
    r = build_running_section(r);
    r = build_enclosure_section(r);
    r = build_backup_section(r);     // s075
    r = build_keyboard_section(r);   // s070

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

// s019: what a dropped file becomes. One box, because the other choice is
// the modifier, not a second setting.
int PreferencesWindow::build_enclosure_section(int row) {
    m_drop_links_check.set_label("Link dropped files instead of copying them");
    m_drop_links_check.signal_toggled().connect([this]() {
        if (!m_setting) m_sig_drop_links.emit(m_drop_links_check.get_active());
    });
    row = add_heading("Dropped files", row);
    m_grid.attach(m_drop_links_check, 1, row++, 1, 1);
    row = add_note("Off: a file dropped on a note is copied into the jots "
                   "folder, so the note keeps it even if the original moves. "
                   "On: the note links to the file where it is, and the drawer "
                   "says when it has changed or gone missing. Hold Shift "
                   "while dropping to do the other one.", row);
    return row;
}

// s070 (Scott's kids: keys "should be programmable"). The count, the way in
// (the Keyboard Shortcuts window, editing) and the one way back for all of
// them. One key's way back is the ↺ on its row in that window.
// s075 (Scott: "a backup process that can be automatic"; "rsync is the linux
// way"; "a pref for where the backups go"). One check box, where, and what
// happened last -- with the two verbs beside it.
int PreferencesWindow::build_backup_section(int row) {
    row = add_heading("Backups", row);
    m_backup_check.set_label("Back up the open jots folder automatically");
    m_backup_check.signal_toggled().connect([this]() {
        if (!m_setting) m_sig_backup_on.emit(m_backup_check.get_active());
    });
    m_grid.attach(m_backup_check, 1, row++, 1, 1);

    auto* where = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_backup_where.set_xalign(0.0f);
    m_backup_where.set_hexpand(true);
    m_backup_where.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    m_backup_where.set_selectable(true);
    where->append(m_backup_where);
    m_backup_choose.set_tooltip_text("Keep the backups somewhere else -- another drive, a synced folder");
    m_backup_choose.signal_clicked().connect([this]() { m_sig_backup_choose.emit(); });
    where->append(m_backup_choose);
    m_backup_reset.set_tooltip_text("Back to ~/.local/share/jot/backups");
    m_backup_reset.signal_clicked().connect([this]() { m_sig_backup_reset.emit(); });
    where->append(m_backup_reset);
    row = add_row("Where", *where, row);

    auto* now = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_backup_says.set_xalign(0.0f);
    m_backup_says.set_hexpand(true);
    m_backup_says.set_wrap(true);
    now->append(m_backup_says);
    m_backup_now.signal_clicked().connect([this]() { m_sig_backup_now.emit(); });
    now->append(m_backup_now);
    m_backup_restore.set_tooltip_text("Bring back the folder as it was on a day -- as a new folder");
    m_backup_restore.signal_clicked().connect([this]() { m_sig_backup_restore.emit(); });
    now->append(m_backup_restore);
    m_grid.attach(*now, 1, row++, 1, 1);

    row = add_note("A copy of the whole folder for each day, made with rsync: today's is "
                   "refreshed as you work (at most hourly) and when jot quits. Files that did "
                   "not change are shared between days, so a day costs only what changed. "
                   "Kept: the last 7 days, and one for each of the 4 weeks before. Each day is "
                   "a plain folder you can open in Files.", row);
    return row;
}

void PreferencesWindow::set_backup(bool on, const std::string& where, bool is_default,
                                   const std::string& status, bool busy, bool has_folder) {
    m_setting = true;
    m_backup_check.set_active(on);
    m_setting = false;
    m_backup_where.set_text(where);
    m_backup_where.set_tooltip_text(where);
    m_backup_reset.set_sensitive(!is_default);
    m_backup_says.set_text(status);
    m_backup_now.set_sensitive(has_folder && !busy);
    m_backup_now.set_label(busy ? "Backing Up\u2026" : "Back Up Now");
    m_backup_restore.set_sensitive(has_folder);
}

int PreferencesWindow::build_keyboard_section(int row) {
    row = add_heading("Keyboard", row);
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_keys_says.set_xalign(0.0f);
    m_keys_says.set_hexpand(true);
    box->append(m_keys_says);
    m_keys_edit.signal_clicked().connect([this]() { m_sig_edit_keys.emit(); });
    box->append(m_keys_edit);
    m_keys_reset.signal_clicked().connect([this]() {
        auto alert = Gtk::AlertDialog::create(
            "Put back jot's own keys for " +
            (m_keys_changed == 1 ? std::string("1 shortcut") : std::to_string(m_keys_changed) + " shortcuts") +
            "?");
        alert->set_detail("Every shortcut you changed goes back to the key jot came with.");
        alert->set_buttons({"Cancel", "Reset All"});
        alert->set_cancel_button(0);
        alert->set_default_button(1);
        alert->choose(*this, [this, alert](Glib::RefPtr<Gio::AsyncResult>& res) {
            try {
                if (alert->choose_finish(res) == 1) m_sig_reset_keys.emit();
            } catch (const Glib::Error&) {}
        });
    });
    box->append(m_keys_reset);
    m_grid.attach(*box, 1, row++, 1, 1);
    row = add_note("Change any shortcut in the Keyboard Shortcuts window (Ctrl+?): Edit, "
                   "click it, press the new key. The menus, the cheat sheet and the guide "
                   "show your keys. The global capture key is under Capture.", row);
    set_keys_changed(0);
    return row;
}

void PreferencesWindow::set_keys_changed(int n) {
    m_keys_changed = n;
    m_keys_says.set_text(n == 0 ? "All shortcuts are jot's own"
                                : (n == 1 ? std::string("1 shortcut changed")
                                          : std::to_string(n) + " shortcuts changed"));
    m_keys_reset.set_sensitive(n > 0);
}

// s050b (Scott: "allow the user to choose their highlight colors ... use
// system coloring or custom"). The Mac's row of accent dots: the first is
// System (a multicolour dot -- follow GNOME), then GNOME's own nine, then a
// colour button for anything else. The line under it says what is in force.
int PreferencesWindow::build_look_section(int row) {
    auto css = Gtk::CssProvider::create();
    std::string sheet =
        ".jot-swatch { min-width: 22px; min-height: 22px; padding: 0; border-radius: 999px; "
        "border: 2px solid alpha(currentColor, 0.15); box-shadow: none; }\n"
        ".jot-swatch:checked { border: 2px solid @theme_bg_color; "
        "box-shadow: 0 0 0 2px alpha(currentColor, 0.75); }\n"
        ".jot-swatch-system { background-color: #3584e4; background-image: linear-gradient(135deg, "
        "#3584e4, #9141ac 30%, #e62d42 50%, #ed5b00 65%, #c88800 80%, #3a944a); }\n";
    for (const auto& p : core::accent_presets())
        sheet += std::string(".jot-swatch-") + p.name + " { background-color: " + p.hex +
                 "; background-image: none; }\n";
    css->load_from_data(sheet);
    Gtk::StyleContext::add_provider_for_display(get_display(), css,
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    auto* line = Gtk::make_managed<widgets::Box>("prefs.accent.row", Gtk::Orientation::HORIZONTAL, 8);
    auto add = [&](const std::string& key, const std::string& hex, const std::string& tip) {
        auto* b = Gtk::make_managed<widgets::ToggleButton>("prefs.accent." + key);
        b->add_css_class("jot-swatch");
        b->add_css_class("jot-swatch-" + key);
        b->set_valign(Gtk::Align::CENTER);
        b->set_tooltip_text(tip);
        b->signal_toggled().connect([this, b, hex]() {
            if (m_setting) return;
            if (!b->get_active()) { show_accent(); return; }   // a pressed dot stays pressed
            choose_accent(hex);
        });
        m_swatches.push_back({hex, b});
        line->append(*b);
    };
    add("system", "", "System — follow GNOME's accent colour (Settings › Appearance)");
    for (const auto& p : core::accent_presets()) add(p.name, p.hex, p.name);

    m_custom = Gtk::make_managed<widgets::ColorDialogButton>("prefs.accent.custom",
                                                             Gtk::ColorDialog::create());
    m_custom->get_dialog()->set_with_alpha(false);
    m_custom->get_dialog()->set_title("Highlight colour");
    m_custom->set_tooltip_text("Any colour");
    m_custom->set_valign(Gtk::Align::CENTER);
    m_custom->property_rgba().signal_changed().connect([this]() {
        if (m_setting) return;
        const Gdk::RGBA c = m_custom->get_rgba();
        const std::string hex = core::accent_css(c.get_red(), c.get_green(), c.get_blue());
        if (!hex.empty()) choose_accent(hex);
    });
    line->append(*m_custom);

    row = add_heading("Appearance", row);
    row = add_row("Highlight colour", *line, row);
    m_accent_says.set_halign(Gtk::Align::START);
    m_accent_says.set_xalign(0.0f);
    m_accent_says.set_wrap(true);
    m_accent_says.add_css_class("dim-label");
    m_grid.attach(m_accent_says, 1, row++, 1, 1);
    show_accent();
    return row;
}

void PreferencesWindow::choose_accent(const std::string& hex) {
    m_accent = hex;
    show_accent();
    m_sig_accent.emit(hex);
}

void PreferencesWindow::set_accent(const std::string& chosen, const std::string& desktop) {
    m_accent = chosen;
    m_desktop_accent = desktop;
    show_accent();
}

void PreferencesWindow::show_accent() {
    m_setting = true;
    bool any = false;
    for (auto& sw : m_swatches) {
        std::string a = m_accent, b = sw.hex;
        for (auto* s : {&a, &b})
            for (auto& ch : *s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const bool on = (a == b);
        any = any || on;
        sw.button->set_active(on);
    }
    if (m_custom) {   // shows the colour in force, so its dialog opens there
        Gdk::RGBA c;
        const std::string now = m_accent.empty()
            ? (m_desktop_accent.empty() ? std::string(core::kDefaultAccent) : m_desktop_accent)
            : m_accent;
        if (c.set(now)) m_custom->set_rgba(c);
    }
    m_setting = false;
    const std::string name = core::accent_name(m_accent);
    if (m_accent.empty()) {
        const std::string d = core::accent_name(m_desktop_accent);
        m_accent_says.set_text("System: follows the accent you pick in GNOME Settings › Appearance"
                               " (now " + (d == "Custom" ? m_desktop_accent : d) + ").");
    } else {
        m_accent_says.set_text(name == "Custom" ? "Custom: " + m_accent + "." : name + ".");
    }
    (void)any;
}

void PreferencesWindow::set_drop_links_on(bool on) {
    m_setting = true;
    m_drop_links_check.set_active(on);
    m_setting = false;
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
