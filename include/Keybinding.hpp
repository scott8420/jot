#pragma once
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// keys -- jot's global capture shortcut, written into GNOME's own keybinding
// settings. The performing half; core/Hotkey.hpp is the deciding half.
//
// WHY GSETTINGS AND NOT THE GlobalShortcuts PORTAL. The portal was the other
// candidate and it was checked (s014) rather than assumed:
//
//   * It cannot fire when jot is NOT RUNNING. The portal delivers an Activated
//     signal to a live D-Bus session, so a hotkey through it could never reach
//     the cold-capture spool s013 just built -- which is the whole point of a
//     capture key.
//   * The APPLICATION DOES NOT CHOOSE THE CHORD. `BindShortcuts` proposes a
//     preferred trigger and the desktop asks the user; on GNOME the result is
//     edited in Settings, not here. "Settable from jot's own preferences" is
//     precisely what it cannot do.
//   * GNOME only grew an implementation in 48, and it keys by application id
//     rather than by session, which is a deviation applications have already
//     been bitten by.
//
// Writing gnome-settings-daemon's `custom-keybindings` is the mechanism GNOME's
// OWN Settings panel uses for exactly this, it launches a command whether or
// not the app is running, and the user can see and edit the result in the place
// they already look for it. The cost is honest and stated in the UI: it is
// GNOME-specific, and on another desktop the feature reports itself
// unavailable rather than pretending.
//
// NOTHING HERE ABORTS ON A MISSING SCHEMA. Constructing a Gio::Settings for a
// schema that is not installed is FATAL -- g_settings_new kills the process --
// so every access goes through a SettingsSchemaSource lookup first. On KDE,
// on a bare WM, or in a sandbox with no GNOME schemas, jot must come up with
// the feature greyed out, not fall over.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::keys {

// What jot's slot currently holds. `supported` false means this desktop has no
// such settings at all and every other field is meaningless.
struct Status {
    bool        supported = false;   // the GNOME schemas are installed
    bool        installed = false;   // jot's slot is listed AND has a binding
    std::string accel;               // GTK accel form, e.g. "<Super><Shift>n"
    std::string command;             // what the desktop will run
    bool        ours = false;        // the command still looks like jot's
};

Status current();

// Write (or rewrite) the slot and make sure it is listed. Returns false and
// fills `err` with something a person can read.
//
// ORDER: the slot's three keys are written BEFORE the path joins the list. A
// listed slot is one gnome-settings-daemon will try to honour, so the moment it
// becomes visible it has to be complete -- listed-then-filled has a window in
// which a keypress runs an empty command.
bool install(const std::string& accel, const std::string& command, std::string* err);

// Unlist it, then clear it -- the reverse order, for the same reason.
bool uninstall(std::string* err);

// Who else already claims this chord. Empty means nobody GNOME will admit to.
// Advisory, NOT a veto: the scan reads the schemas it knows about, and a
// shortcut belonging to an extension or another application's own grab is
// invisible to it. So jot reports what it found and still lets the user decide,
// rather than refusing on evidence it knows is incomplete.
struct Conflict {
    std::string where;   // "Windows", "GNOME Shell", "Another custom shortcut"
    std::string what;    // the setting's own name, prettified
};
std::vector<Conflict> conflicts(const std::string& accel);

// This binary's absolute path, for the command line the desktop will run.
// /proc/self/exe, because the hotkey has to name the jot that is running -- a
// build tree, an installed copy, or a second checkout are all real, and "jot"
// on $PATH may be none of them.
std::string exe_path();

}  // namespace jot::keys
