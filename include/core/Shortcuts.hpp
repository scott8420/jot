#pragma once
// core::Shortcuts -- the SHORTCUT-REGISTRY paradigm: a key binding and the row
// that advertises it are ONE fact, declared once.
//
// The stone this carries: a shortcut has two consumers that must never disagree
// -- the wiring (set_accels_for_action) and the documentation (the shortcuts
// window). Declare them separately and they drift the first time a key changes;
// the app then lies to its user about its own keys. So the registry is the
// single source, and both consumers READ it: App wires from it, ShortcutsDialog
// renders from it. Add a shortcut in one place or it doesn't exist.
//
// It lives in core/ because it is GTK-FREE -- pure data plus string helpers
// (the two-layer seam: core knows nothing of widgets). That's what lets the
// selftest guard it headless: a pure test proves no two chords collide, which is
// a class of bug that otherwise only shows up as a mysteriously dead key.
//
// DOC-ONLY ROWS are half the point. Mouse gestures (drag, Ctrl+scroll) and keys
// a widget handles itself are not GActions and cannot be accelerators -- but a
// reader needs them just as much. A row with an empty `action` documents without
// binding, so the reference stays complete without lying to the accel table.
//
// (Adapted from Sudoku's registry, itself from Folio's s98. jot carries the
// shape, not the content -- these bindings are the seed's own.)
#include <map>
#include <string>
#include <vector>

namespace jot::core {

struct ShortcutSpec {
    std::string              section;      // group heading, e.g. "General" (authored A-Z)
    std::string              action;       // "win.about" -- empty => doc-only (not a GAction)
    std::vector<std::string> accels;       // GTK accel form(s); wired iff action set
    std::string              keys;         // explicit display; empty => derived from accels
    std::string              description;

    std::string display_keys() const;      // `keys`, else accels formatted + joined
};

// "<Ctrl><Shift>n" -> "Ctrl+Shift+N". Modifiers in canonical order; named keys
// map to glyphs where one reads better.
std::string format_accel(const std::string& accel);

// The registry -- authored section (A-Z) -> row, so a consumer walks it linearly
// and starts a heading whenever the section changes.
//
// s070: what jot SHIPS is shortcut_defaults(); shortcut_registry() is those
// with the user's changes laid over them (set_key_overrides). Every reader --
// the accelerators, the menus, the cheat sheet, the shortcuts window, the
// guide's Try keys -- reads the registry, so a changed key shows everywhere.
const std::vector<ShortcutSpec>& shortcut_registry();
const std::vector<ShortcutSpec>& shortcut_defaults();

// ── s070: keys you can change ─────────────────────────────────────────────
// action -> the accels it has instead of jot's own. An EMPTY list means "no
// key". Only changes are kept, so a later jot's new defaults still arrive.
using KeyOverrides = std::map<std::string, std::vector<std::string>>;

void set_key_overrides(const KeyOverrides& o);   // takes clean_overrides(o)
const KeyOverrides& key_overrides();

// Drops what cannot stand: an action jot does not have (or a Diagnostics one),
// a key that is not fit (key_problem), an entry equal to the default.
KeyOverrides clean_overrides(const KeyOverrides& o);

// Can `accel` be `action`'s key?
struct KeyCheck {
    enum Kind { Ok, Same, NotAKey, NeedsModifier, TextEditing, Reserved, Taken };
    Kind        kind = Ok;
    std::string other;        // Taken: the action that has it
    std::string other_words;  // Taken / Reserved: what it does there
    std::string words;        // a sentence for the window
};
KeyCheck check_new_key(const std::string& action, const std::string& accel);

// The overrides with `action`'s key set to `accel` alone ("" = no key). With
// `swap`, whoever had `accel` loses it (keeping any other key it had).
KeyOverrides with_key(const KeyOverrides& o, const std::string& action,
                      const std::string& accel, bool swap);
// The overrides with `action` back to jot's own key.
KeyOverrides without_override(const KeyOverrides& o, const std::string& action);
// The same, but safe: if another verb has since been given one of `action`'s
// own keys, that verb gives it up (its other keys stay). `took_from` names them.
KeyOverrides reset_key(const KeyOverrides& o, const std::string& action,
                       std::vector<std::string>* took_from = nullptr);

// An accel collides when two+ specs claim it and at least one is a wired
// GAction (a GAction accel fires regardless of focus, so anything else on that
// chord is shadowed). Sorted, unique; empty => clean.
std::vector<std::string> find_accel_collisions();

// s016c. An application accelerator is heard BEFORE the focused widget, so a
// global chord that a text box also uses is taken away from every text box in
// the window. jot learned it the expensive way: Ctrl+Delete was delete-note,
// and pressing it to delete a WORD in the body deleted the NOTE, subtree and
// all, with no prompt and no undo.
//
// True when `accel` is a chord GTK's text widgets use for editing or moving.
// The registry is checked against it under the selftest, so the rule is a
// failing test rather than a thing to remember.
bool steals_text_editing(const std::string& accel);

// ── s069: the Keyboard Shortcuts window's rows ────────────────────────────
// The window shows KEYS only -- the mouse rows (drags, clicks) are the cheat
// sheet's. Each row's keys come as KEYCAPS: alternatives, each a list of caps
// ("Ctrl", "Shift", "L"). Built from the registry, so the window, the accels
// and the cheat sheet still read one fact.
struct KeyRow {
    std::string section;                          // one of key_sections()
    std::string description;
    std::string where;                            // "in the tree", "in the note" -- or ""
    std::vector<std::vector<std::string>> keys;   // alternatives -> caps
    std::string action;                           // "" for a doc-only row
};

// "<Ctrl><Shift>l" -> {"Ctrl", "Shift", "L"}. "<Ctrl>plus" -> {"Ctrl", "+"}.
std::vector<std::string> accel_keycaps(const std::string& accel);

// "Ctrl+Z / Ctrl+Shift+Z" -> {{"Ctrl","Z"}, {"Ctrl","Shift","Z"}}; a "+" key
// written "Ctrl++" stays a "+". Empty when the text is a gesture, not keys.
std::vector<std::vector<std::string>> literal_keycaps(const std::string& keys);

// The window's sections, in the order someone learns them: General, Notes,
// Todos, View, Writing -- Diagnostics last. (The registry itself is A-Z.)
const std::vector<std::string>& key_sections();

// Every keyed row, grouped by key_sections() order, registry order within.
// s070: with `unkeyed`, also the verbs with no key (they can be given one) --
// never Diagnostics' unkeyed or a doc-only row's.
std::vector<KeyRow> key_rows(bool unkeyed = false);

// 0 = not found; 1 = found by its words (description, where, section);
// 2 = found by its KEYS ("ctrl+m", "f1", "shift l") -- the window lights the caps.
// Trimmed, ASCII case-blind, every word must hit. An empty query is 1.
int key_row_match(const KeyRow& r, const std::string& query);

// Which of the row's alternatives the KEYS in `query` point at -- the ones the
// window lights. "shift" on Ctrl+Z / Ctrl+Shift+Z lights only the second. All
// false when the row was not found by its keys.
std::vector<bool> key_lit_alternatives(const KeyRow& r, const std::string& query);
std::vector<std::string> find_text_editing_steals();   // registry accels that do

}  // namespace jot::core
