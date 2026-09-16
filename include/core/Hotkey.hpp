#pragma once
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Hotkey -- what a GLOBAL capture shortcut is, decided here and nowhere
// else. GTK-free, so every rule below is under the selftest.
//
// WHY THIS IS NOT JUST "call Gio::Settings and be done". Installing the hotkey
// means editing ANOTHER APPLICATION'S CONFIGURATION -- gnome-settings-daemon's
// list of custom keybindings, which also holds every shortcut the user made by
// hand. The operations that touch that list (add ours, remove ours, leave
// everyone else's exactly as they were, and never list a slot twice) are list
// algebra, and list algebra written inline inside a settings call is list
// algebra nobody can test. The dangerous failure is not "jot's hotkey does not
// work"; it is "jot dropped somebody's other shortcuts on the way past."
//
// So: this file decides WHAT to write, Keybinding.cpp performs it. The same
// split as core::project / Desktop.cpp, for the same reason.
//
// The second job here is judging a CHORD. A window accelerator competes with
// jot's own keys; a global one competes with the whole desktop, so a bare `n`
// grabbed system-wide would eat the letter n everywhere on the machine, and
// the user's way out of that is a hotkey editor they can no longer type into.
// hotkey_objection() is that judgement, in one place, as a string the UI shows
// rather than a bool it has to invent a sentence for.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// An accel split into its parts. Deliberately the four modifiers GNOME's own
// keybinding editor recognises and no more -- Hyper and the Modn aliases are
// folded into these on the way in.
struct AccelParts {
    bool ctrl  = false;
    bool alt   = false;
    bool shift = false;
    bool super = false;
    std::string key;   // GTK key NAME ("n", "comma", "F5"), never a glyph

    bool any_modifier() const { return ctrl || alt || shift || super; }
    // Shift alone does not qualify: <Shift>n is the letter N, and grabbing it
    // globally is the same catastrophe as grabbing n.
    bool qualifying_modifier() const { return ctrl || alt || super; }
};

// "<Primary><shift>N" -> parts. Liberal about what it accepts (case, <Ctl>,
// <Primary>, <Mod1>, <Meta>) because these strings come from GNOME's own
// dconf as well as from jot, and the two spell the same chord differently.
AccelParts parse_accel_parts(const std::string& accel);

// The same chord, written one way: modifiers in a fixed order, GTK's spelling,
// single-character keys lowercased. Empty when there is no key part at all --
// which is what an unparseable string, a bare modifier, or "" all come back as.
//
// This is what makes comparison possible: "<Primary>J" and "<Control>j" are one
// shortcut, and a conflict scan that compares raw strings misses it.
std::string canonical_accel(const std::string& accel);

// Two spellings, one chord?  Empty never equals anything, including empty.
bool accels_equal(const std::string& a, const std::string& b);

// Why this chord is not fit to be a GLOBAL shortcut -- empty string means it is
// fine. The text is written to be shown to a person as-is.
std::string hotkey_objection(const std::string& accel);

// ── the slot in gnome-settings-daemon's list ────────────────────────────────
// A custom keybinding is a dconf PATH holding three keys (name, command,
// binding), plus that path's membership in one `as` list. jot owns exactly one
// path and it is a constant, not a generated id: a path that changed between
// runs would leave an orphan slot behind on every upgrade, and the user would
// find a growing pile of dead jot entries in GNOME Settings.
const char* custom_keybindings_prefix();   // ".../media-keys/custom-keybindings/"
std::string jot_slot_path();               // the prefix + jot's own leaf, trailing slash

bool has_slot(const std::vector<std::string>& list, const std::string& path);

// Ours added / removed, EVERYTHING ELSE UNTOUCHED AND IN ORDER. with_slot is
// idempotent and de-duplicates ours if a previous write (or a hand edit) left
// it in twice; neither function reorders, rewrites or drops a stranger's entry.
std::vector<std::string> with_slot(const std::vector<std::string>& list,
                                   const std::string& path);
std::vector<std::string> without_slot(const std::vector<std::string>& list,
                                      const std::string& path);

// The command gnome-settings-daemon will run. `--capture` with NO TEXT, which
// is the one form that presents: the hotkey means "let me type", and the
// spool/forward machinery behind it decides whether that is a running jot or a
// cold one.
//
// The exe path is passed in (the caller resolves /proc/self/exe) and QUOTED IF
// IT NEEDS IT -- gnome-settings-daemon shell-parses the string, so a build tree
// under a directory with a space in it would otherwise spawn the wrong thing
// and fail in silence, at a keypress, with no terminal to say so.
std::string capture_command(const std::string& exe);

// The visible half of the same fact: does this command look like OUR capture
// command, whoever's jot binary it names? Used to tell "the hotkey is set up"
// from "something else is sitting in jot's slot" without demanding the exact
// path match, because a rebuilt jot legitimately moves.
bool looks_like_capture_command(const std::string& command);

}  // namespace jot::core
