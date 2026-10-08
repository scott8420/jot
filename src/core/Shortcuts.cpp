#include "core/Shortcuts.hpp"
#include "core/Hotkey.hpp"   // s070: canonical_accel, accels_equal, parse_accel_parts

#include <algorithm>
#include <cctype>
#include <map>

namespace jot::core {

// ─────────────────────────────────────────────────────────────────────────────
// Accel formatting
// ─────────────────────────────────────────────────────────────────────────────
namespace {

std::string lower(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) o += char(std::tolower((unsigned char)c));
    return o;
}

// GTK key name -> display glyph. Single alpha keys uppercase; named keys map to a
// glyph where one reads better, otherwise the name is prettified.
std::string map_key(const std::string& key) {
    if (key.empty()) return "";
    static const std::map<std::string, std::string> named = {
        {"comma", ","},     {"period", "."},     {"question", "?"},
        {"slash", "/"},     {"bracketleft", "["}, {"bracketright", "]"},     {"minus", "\u2212"}, {"plus", "+"},
        {"equal", "="},     {"space", "Space"},
        {"Return", "Enter"},{"Escape", "Esc"},
        {"Left", "\u2190"}, {"Right", "\u2192"}, {"Up", "\u2191"}, {"Down", "\u2193"},
        {"Delete", "Del"},  {"BackSpace", "Backspace"},
        {"Page_Up", "Page Up"}, {"Page_Down", "Page Down"},
        {"KP_Add", "Keypad +"}, {"KP_Subtract", "Keypad \u2212"}, {"KP_0", "Keypad 0"},
    };
    auto it = named.find(key);
    if (it != named.end()) return it->second;
    if (key.size() == 1) {
        std::string s = key;
        s[0] = char(std::toupper((unsigned char)s[0]));
        return s;
    }
    std::string s = key;
    for (char& c : s)
        if (c == '_') c = ' ';
    return s;
}

// Split an accel into its modifier flags + bare key name. Shared by
// format_accel and any future consumer so they can't disagree.
struct Parsed { bool ctrl = false, alt = false, shift = false, super = false; std::string key; };

Parsed parse_accel(const std::string& accel) {
    Parsed p;
    std::size_t i = 0;
    while (i < accel.size()) {
        if (accel[i] == '<') {
            std::size_t j = accel.find('>', i);
            if (j == std::string::npos) break;
            const std::string ml = lower(accel.substr(i + 1, j - i - 1));
            if (ml == "ctrl" || ml == "control" || ml == "primary") p.ctrl = true;
            else if (ml == "alt" || ml == "mod1")                   p.alt = true;
            else if (ml == "shift")                                 p.shift = true;
            else if (ml == "super" || ml == "meta" || ml == "mod4") p.super = true;
            i = j + 1;
        } else {
            p.key = accel.substr(i);
            break;
        }
    }
    return p;
}

}  // namespace

std::string format_accel(const std::string& accel) {
    const Parsed p = parse_accel(accel);
    const std::string k = map_key(p.key);
    std::string out;
    auto add = [&](const std::string& s) {
        if (!out.empty()) out += "+";
        out += s;
    };
    if (p.ctrl)  add("Ctrl");
    if (p.alt)   add("Alt");
    if (p.shift) add("Shift");
    if (p.super) add("Super");
    if (!k.empty()) add(k);
    return out;
}

std::string ShortcutSpec::display_keys() const {
    if (!keys.empty()) return keys;
    std::string out;
    for (const auto& a : accels) {
        if (!out.empty()) out += "  /  ";
        out += format_accel(a);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// The registry  (authored section [A-Z] -> row)
// ─────────────────────────────────────────────────────────────────────────────
const std::vector<ShortcutSpec>& shortcut_defaults() {
    // Fields: {section, action, accels, keys, description}
    // action non-empty => wired via set_accels_for_action. keys empty => derived.
    // Deliberately small -- a seed proves the SHAPE, not a product's key set. It
    // carries one doc-only row so that half of the paradigm is exercised too.
    static const std::vector<ShortcutSpec> kReg = {
        // ── Diagnostics ───────────────────────────────────────────────────────
        {"Diagnostics", "win.dump-registry", {"<Ctrl>d"}, "",
         "Dump the widget registry to the log"},
        {"Diagnostics", "win.dump-nodes",    {"<Ctrl><Shift>d"}, "",
         "Dump the node tree to the log"},

        // ── General ───────────────────────────────────────────────────────────
        {"General", "win.new-jots",      {"<Ctrl><Shift>o"}, "", "Start a new set of jots"},
        {"General", "win.open-jots",     {"<Ctrl>o"}, "", "Open a folder of jots"},
        {"General", "win.close-jots",    {"<Ctrl><Shift>w"}, "",
         "Close the jots folder -- no folder open until you open one"},
        {"General", "win.save-as",       {"<Ctrl><Shift>s"}, "",
         "Write a second jots folder with everything in it, and switch to it"},
        {"General", "win.save-all",      {"<Ctrl>s"}, "",
         "Save everything now (structure already saves itself)"},
        {"General", "win.preferences",   {"<Ctrl>comma"}, "",
         "Preferences \u2014 including the global capture shortcut"},
        {"General", "win.shortcuts",     {"<Ctrl>question", "<Ctrl>slash"}, "",
         "Keyboard shortcuts (this window)"},
        // s030. Ctrl+H: the cheat sheet. No text box binds it.
        // s067: F1 is GNOME's HELP key -- the reason About lost it in s016c --
        // and now jot has a Help proper (the guide), so F1 moves there. The
        // cheat sheet keeps Ctrl+H; the guide's letter-row twin is Ctrl+Shift+H
        // (F-keys are not dependable on Asahi), listed first. Both open the one
        // Help window, on its Guide or its Cheat Sheet page.
        {"General", "win.help",          {"<Ctrl><Shift>h", "F1"}, "",
         "jot Help: what each part of jot is for, and how to use it"},
        {"General", "win.cheat-sheet",   {"<Ctrl>h"}, "",
         "Cheat sheet: everything jot does, one line each"},
        // No About row: it had F1, which every GNOME app reserves for HELP, and
        // pressing it expecting help and getting a credits window teaches that
        // jot's keys are not the ones you already know (s016c). A row with no
        // key has nothing to say in a shortcuts list.
        {"General", "win.quit",          {"<Ctrl>q"}, "", "Quit"},

        // ── Mouse ─────────────────────────────────────────────────────────────
        // Doc-only: a gesture is not a GAction, but the reader still needs it.
        // The two drags are the whole of s002's drag-and-drop vocabulary, and
        // neither is discoverable without being told once.
        {"Mouse", "", {}, "Drag onto a row's middle", "Make it a child of that note"},
        {"Mouse", "", {}, "Drag onto a row's top or bottom edge",
         "Drop it above or below that note, among its siblings"},
        {"Mouse", "", {}, "Drag a note below the list", "Move it back to the top level"},

        // ── Notes ─────────────────────────────────────────────────────────────
        {"Notes", "win.capture",        {"<Ctrl><Shift>Return"}, "",
         "Jump to the capture line in the header"},
        {"Notes", "win.new-note",       {"<Ctrl>n"}, "", "New note at the top level"},
        {"Notes", "win.copy-link",      {"<Ctrl><Shift>c"}, "",
         "Copy a link to this note, ready to paste into another"},
        {"Notes", "win.import-md",      {"<Ctrl><Shift>m"}, "",
         "Import markdown files as new notes"},
        {"Notes", "win.import-md-folder", {}, "",
         "Import a folder of markdown as notes (Notes menu)"},
        {"Notes", "win.new-child",      {"<Ctrl><Shift>n"}, "",
         "New note under the selected one"},
        // Doc-only: Delete is bound by the TREE, not the application, so it
        // only fires while the tree has focus -- the Files convention. It was
        // Ctrl+Delete app-wide until s016c, which a text box uses to delete a
        // word; see steals_text_editing().
        {"Notes", "", {}, "Delete (in the tree)",
         "Delete the selected note and everything under it"},
        // s045: doc-only, bound by the TREE like Delete (focus-scoped), so the
        // note body keeps Ctrl+Z, Enter and Tab for itself.
        {"Notes", "", {}, "Ctrl+Z / Ctrl+Shift+Z", "Undo / redo any change to notes and todos (not in a text box)"},
        {"Notes", "", {}, "Ctrl+click / Shift+click (in the tree)", "Select several notes -- Delete, Tick, Flag ... act on all"},
        {"Notes", "", {}, "Enter (in the tree)", "New note below the selected one, ready to name"},
        {"Notes", "", {}, "Tab / Shift+Tab (in the tree)", "Indent / outdent the selected note"},
        {"Notes", "", {}, "Alt+Up / Alt+Down (in the tree)", "Move the selected note up / down"},
        {"Notes", "win.toggle-protect", {"<Ctrl>l"}, "",
         "Protect or unprotect the selected note"},
        {"Notes", "win.rename-note",    {"<Ctrl>r", "F2"}, "",
         "Rename the selected note in the tree"},
        // s028. OmniFocus's Clean Up is Cmd-K; Ctrl+K is the body's Link, so
        // the shifted twin -- not a text-editing chord (steals_text_editing).
        {"Notes", "win.clean-up",       {"<Ctrl><Shift>k"}, "",
         "Clean Up: take everything filed or done off the Inbox"},
        // s029. OmniFocus's Move is Cmd-Shift-M; Ctrl+Shift+M is Import here
        // (s021b), so the plain chord -- nothing in a text box binds Ctrl+M.
        {"Notes", "win.move-to",        {"<Ctrl>m"}, "",
         "Move the selected note: type where it goes"},

        // ── Todos ─────────────────────────────────────────────────────────────
        // A todo IS a note (D2), so these three are note verbs and they sit
        // next to the note ones. There is no priority key because there is no
        // priority field: you drag it up the list instead.
        {"Todos", "win.toggle-todo",    {"<Ctrl>t"}, "",
         "Make the selected note a todo, or stop it being one"},
        {"Todos", "win.toggle-done",    {"<Ctrl>Return"}, "",
         "Tick or untick the selected todo"},
        {"Todos", "win.toggle-flag",    {"<Ctrl><Shift>f"}, "",
         "Flag the selected todo -- \"this one, today\""},
        // s037. OmniFocus's Mark Reviewed. Nothing in a text box binds it.
        // s037b: menu / button verbs, no key -- listed so the sheet can name them.
        {"Todos", "win.toggle-project", {}, "",
         "Say the selected note is a project, or never one"},
        {"Todos", "win.new-project",    {}, "",
         "New project (the + in the Projects tab)"},
        {"Todos", "win.mark-reviewed",  {"<Ctrl><Shift>r"}, "",
         "Mark the selected project reviewed (in Review: on to the next)"},

        // ── View ──────────────────────────────────────────────
        // Both off is the focus mode: the note alone on screen.
        //
        // Every F-key has a letter-row twin (Scott, s018): on a MacBook under
        // Asahi Fedora the function row is not dependable. The brackets are
        // by SIDE -- [ is the pane on the left, ] the one on the right. The
        // twin is listed FIRST because a menu shows only the first accel, and
        // the menu should show the key that works on the machine jot runs on.
        {"View", "win.toggle-tree",     {"<Ctrl>bracketleft", "F9"}, "",
         "Show or hide the side pane (Notes, Inbox, Today, Tags, Projects)"},
        // s035: tags as contexts. Picks the note's first tag if none is picked.
        {"View", "win.show-tags",       {"<Ctrl><Shift>t"}, "",
         "Tags: everything with a #tag, todos first"},
        // s038: Find across every note. GNOME's find key; nothing in jot's
        // text view binds it.
        {"View", "win.find",            {"<Ctrl>f"}, "",
         "Find: every note with these words, in its name or text"},
        // s038b: perspectives -- J for jump. Nothing in a text view binds it.
        {"View", "win.perspectives",    {"<Ctrl>j"}, "",
         "Perspectives: the Find field's filter menu, open"},
        // s037: Review. P for Projects; pressed again, Review <-> All projects.
        {"View", "win.show-projects",   {"<Ctrl><Shift>p"}, "",
         "Projects: the ones due a review, or all of them by state"},
        // s059: L for timeLine (Ctrl+T is the new todo). Nothing in a text box binds it.
        {"View", "win.timeline",        {"<Ctrl><Shift>l"}, "",
         "Timeline: your work along the days, in the note's place (again: back to the note)"},
        // s066: G for Glance. Nothing in a text box binds it.
        {"View", "win.glance",          {"<Ctrl><Shift>g"}, "",
         "Glance of Today: the day on one card -- Copy, Email, Save .ics"},
        {"View", "win.toggle-drawer",   {"<Ctrl>bracketright", "F10"}, "",
         "Show or hide note details"},
        // s021: Obsidian's key for the same flip. No F-key twin needed.
        {"View", "win.toggle-reading",  {"<Ctrl>e"}, "",
         "Switch the note between Source and Reading"},
        // s022: what editing looks like -- marks only where the cursor is (s026: per run),
        // or every mark on screen.
        {"View", "win.toggle-live",     {"<Ctrl><Shift>e"}, "",
         "Live preview: hide the marks except where the cursor is"},
        // s053b: the browser keys. Ctrl+= is Ctrl++ without Shift.
        {"View", "win.zoom-in",         {"<Ctrl>equal", "<Ctrl>plus"}, "",
         "Bigger text in the note"},
        {"View", "win.zoom-out",        {"<Ctrl>minus"}, "", "Smaller text in the note"},
        {"View", "win.zoom-reset",      {"<Ctrl>0"}, "", "The note's text at 100%"},

        // ── Writing ───────────────────────────────────────────
        // s024: doc-only rows. These are bound on the NOTE, not the app, so
        // they cannot fire from the tree or a rename field; the format bar
        // above the note has the rest (headings, lists, quote, code block).
        {"Writing", "", {}, "Ctrl+B", "Bold -- or unbold (in the note)"},
        {"Writing", "", {}, "Ctrl+I", "Italic -- or not (in the note)"},
        {"Writing", "", {}, "Ctrl+K", "Link: the selection becomes its label (in the note)"},
    };
    return kReg;
}

// ─────────────────────────────────────────────────────────────────────────────
// s070: the registry is the defaults with the user's changes over them
// ─────────────────────────────────────────────────────────────────────────────
namespace {

KeyOverrides& overrides_store() {
    static KeyOverrides o;
    return o;
}

std::vector<ShortcutSpec>& effective_store() {
    static std::vector<ShortcutSpec> v = shortcut_defaults();
    return v;
}

const ShortcutSpec* default_for(const std::string& action) {
    for (const auto& s : shortcut_defaults())
        if (s.action == action) return &s;
    return nullptr;
}

bool same_accels(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!accels_equal(a[i], b[i])) return false;
    return true;
}

bool is_fkey(const std::string& key) {
    if (key.size() < 2 || key[0] != 'F') return false;
    for (std::size_t i = 1; i < key.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(key[i]))) return false;
    return true;
}

// A written key ("Alt", "Up") against an accel ("<Alt>Up"): the same chord?
// Compared by parts, case-blind, with the written names GTK spells otherwise.
bool caps_are(const std::vector<std::string>& caps, const std::string& accel) {
    if (caps.empty()) return false;
    const AccelParts p = parse_accel_parts(accel);
    bool ctrl = false, alt = false, shift = false, super = false;
    for (std::size_t i = 0; i + 1 < caps.size(); ++i) {
        const std::string m = lower(caps[i]);
        if (m == "ctrl") ctrl = true;
        else if (m == "alt") alt = true;
        else if (m == "shift") shift = true;
        else if (m == "super") super = true;
        else return false;
    }
    std::string k = lower(caps.back());
    if (k == "enter") k = "return";
    if (k == "del") k = "delete";
    return ctrl == p.ctrl && alt == p.alt && shift == p.shift && super == p.super &&
           k == lower(p.key);
}

// Fit to be an app key at all (who else has it is checked separately).
KeyCheck::Kind fitness(const std::string& accel) {
    const AccelParts p = parse_accel_parts(accel);
    if (p.key.empty()) return KeyCheck::NotAKey;
    if (steals_text_editing(accel)) return KeyCheck::TextEditing;
    if (!p.qualifying_modifier() && !is_fkey(p.key)) return KeyCheck::NeedsModifier;
    return KeyCheck::Ok;
}

}  // namespace

const std::vector<ShortcutSpec>& shortcut_registry() { return effective_store(); }

const KeyOverrides& key_overrides() { return overrides_store(); }

KeyOverrides clean_overrides(const KeyOverrides& o) {
    KeyOverrides out;
    for (const auto& [action, accels] : o) {
        const ShortcutSpec* d = default_for(action);
        if (!d || d->section == "Diagnostics") continue;
        std::vector<std::string> keep;
        for (const auto& a : accels)
            if (fitness(a) == KeyCheck::Ok) keep.push_back(canonical_accel(a));
        if (keep.empty() && !accels.empty()) continue;   // all junk: not a deliberate "no key"
        if (same_accels(keep, d->accels)) continue;      // the default: not a change
        out[action] = keep;
    }
    return out;
}

void set_key_overrides(const KeyOverrides& o) {
    overrides_store() = clean_overrides(o);
    auto& v = effective_store();
    v = shortcut_defaults();
    for (auto& s : v)
        if (auto it = overrides_store().find(s.action); !s.action.empty() && it != overrides_store().end())
            s.accels = it->second;
}

KeyCheck check_new_key(const std::string& action, const std::string& accel) {
    KeyCheck c;
    const std::string pretty = format_accel(accel);
    c.kind = fitness(accel);
    switch (c.kind) {
        case KeyCheck::NotAKey:
            c.words = "That isn't a key jot can use.";
            return c;
        case KeyCheck::TextEditing:
            c.words = pretty + " belongs to the text boxes (typing, moving, copy and paste) -- pick another.";
            return c;
        case KeyCheck::NeedsModifier:
            c.words = pretty + " alone would be typed, not obeyed -- add Ctrl or Alt (an F-key can stand alone).";
            return c;
        default: break;
    }
    // A key the NOTE or the TREE keeps for itself (Bold, Italic, Link; Alt+Up,
    // Enter, Delete in the tree): a doc-only row that names keys. An app key is
    // heard before the focused widget (s016c), so it would take the key away.
    for (const auto& s : shortcut_registry()) {
        if (!s.action.empty() || s.keys.empty()) continue;
        // "Alt+Up / Alt+Down (in the tree)": the bracket says where, not a key.
        std::string written = s.keys;
        if (const auto o2 = written.rfind(" ("); o2 != std::string::npos && written.back() == ')')
            written = written.substr(0, o2);
        std::string what = s.description;
        if (const auto o3 = what.rfind(" (in the "); o3 != std::string::npos && what.back() == ')')
            what = what.substr(0, o3);
        for (const auto& alt : literal_keycaps(written)) {
            if (caps_are(alt, accel)) {
                c.kind = KeyCheck::Reserved;
                c.other_words = what;
                c.words = pretty + " is kept for: " + what + ". Pick another.";
                return c;
            }
        }
    }
    for (const auto& s : shortcut_registry()) {
        if (s.action.empty()) continue;
        for (const auto& a : s.accels) {
            if (!accels_equal(a, accel)) continue;
            if (s.action == action) {
                c.kind = KeyCheck::Same;
                c.words = pretty + " is already its key.";
                return c;
            }
            c.kind = KeyCheck::Taken;
            c.other = s.action;
            c.other_words = s.description;
            c.words = pretty + " is already: " + s.description + ".";
            return c;
        }
    }
    c.kind = KeyCheck::Ok;
    return c;
}

KeyOverrides with_key(const KeyOverrides& o, const std::string& action,
                      const std::string& accel, bool swap) {
    KeyOverrides out = o;
    out[action] = accel.empty() ? std::vector<std::string>{}
                                : std::vector<std::string>{canonical_accel(accel)};
    if (swap && !accel.empty()) {
        for (const auto& s : shortcut_registry()) {
            if (s.action.empty() || s.action == action) continue;
            std::vector<std::string> left;
            bool had = false;
            for (const auto& a : s.accels) {
                if (accels_equal(a, accel)) had = true;
                else left.push_back(a);
            }
            if (had) out[s.action] = left;
        }
    }
    return clean_overrides(out);
}

KeyOverrides without_override(const KeyOverrides& o, const std::string& action) {
    KeyOverrides out = o;
    out.erase(action);
    return out;
}

KeyOverrides reset_key(const KeyOverrides& o, const std::string& action,
                       std::vector<std::string>* took_from) {
    KeyOverrides out = without_override(o, action);
    const ShortcutSpec* d = default_for(action);
    if (!d) return clean_overrides(out);
    for (const auto& [other, accels] : o) {
        if (other == action) continue;
        std::vector<std::string> left;
        bool had = false;
        for (const auto& a : accels) {
            bool clash = false;
            for (const auto& mine : d->accels)
                if (accels_equal(a, mine)) clash = true;
            if (clash) had = true;
            else left.push_back(a);
        }
        if (had) {
            out[other] = left;
            if (took_from) took_from->push_back(other);
        }
    }
    return clean_overrides(out);
}

// ─────────────────────────────────────────────────────────────────────────────
// s069: rows for the Keyboard Shortcuts window
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> accel_keycaps(const std::string& accel) {
    const Parsed p = parse_accel(accel);
    std::vector<std::string> out;
    if (p.ctrl)  out.push_back("Ctrl");
    if (p.alt)   out.push_back("Alt");
    if (p.shift) out.push_back("Shift");
    if (p.super) out.push_back("Super");
    const std::string k = map_key(p.key);
    if (!k.empty()) out.push_back(k);
    return out;
}

std::vector<std::vector<std::string>> literal_keycaps(const std::string& keys) {
    std::vector<std::vector<std::string>> out;
    const std::string low = lower(keys);
    for (const char* g : {"click", "drag", "scroll"})
        if (low.find(g) != std::string::npos) return {};   // a gesture: the cheat sheet's
    // Alternatives are split on " / ".
    std::vector<std::string> alts;
    std::size_t i = 0;
    while (true) {
        const std::size_t j = keys.find(" / ", i);
        alts.push_back(keys.substr(i, j == std::string::npos ? std::string::npos : j - i));
        if (j == std::string::npos) break;
        i = j + 3;
    }
    for (std::string a : alts) {
        while (!a.empty() && a.front() == ' ') a.erase(a.begin());
        while (!a.empty() && a.back() == ' ') a.pop_back();
        if (a.empty()) continue;
        std::vector<std::string> caps;
        std::string cur;
        for (std::size_t k = 0; k < a.size(); ++k) {
            const char c = a[k];
            // A '+' is a joiner unless it is the key itself ("Ctrl++", or "+").
            if (c == '+' && !cur.empty()) {
                caps.push_back(cur);
                cur.clear();
                continue;
            }
            cur += c;
        }
        if (!cur.empty()) caps.push_back(cur);
        for (const auto& cap : caps)
            if (cap.find(' ') != std::string::npos) return {};   // words, not a key
        if (!caps.empty()) out.push_back(std::move(caps));
    }
    return out;
}

const std::vector<std::string>& key_sections() {
    static const std::vector<std::string> kSections = {
        "General", "Notes", "Todos", "View", "Writing", "Diagnostics",
    };
    return kSections;
}

std::vector<KeyRow> key_rows(bool unkeyed) {
    std::vector<KeyRow> out;
    for (const auto& sec : key_sections()) {
        for (const auto& s : shortcut_registry()) {
            if (s.section != sec) continue;
            KeyRow r;
            r.section = s.section;
            r.action = s.action;
            r.description = s.description;
            if (!s.accels.empty()) {
                for (const auto& a : s.accels) r.keys.push_back(accel_keycaps(a));
            } else if (!s.keys.empty()) {
                // "Delete (in the tree)": the note in brackets is WHERE.
                std::string k = s.keys;
                if (const auto o = k.rfind(" ("); o != std::string::npos && k.back() == ')') {
                    r.where = k.substr(o + 2, k.size() - o - 3);
                    k = k.substr(0, o);
                }
                r.keys = literal_keycaps(k);
            }
            // "... (in the note)" at the end of a description is WHERE too.
            if (r.where.empty()) {
                auto& d = r.description;
                if (const auto o = d.rfind(" (in the "); o != std::string::npos && d.back() == ')') {
                    r.where = d.substr(o + 2, d.size() - o - 3);
                    d = d.substr(0, o);
                }
            }
            // Unkeyed verbs (s070: shown while editing, to be given one) and
            // gestures (never: the cheat sheet's).
            if (r.keys.empty() &&
                !(unkeyed && !s.action.empty() && s.section != "Diagnostics"))
                continue;
            out.push_back(std::move(r));
        }
    }
    return out;
}

int key_row_match(const KeyRow& r, const std::string& query) {
    // Words: split on spaces; a word may itself be "ctrl+m".
    std::vector<std::string> ws;
    {
        std::string cur;
        for (char c : query) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!cur.empty()) ws.push_back(lower(cur));
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) ws.push_back(lower(cur));
    }
    if (ws.empty()) return 1;
    // The keys, as "ctrl+shift+l" per alternative, and the caps one by one.
    std::vector<std::string> combos;
    std::vector<std::string> caps;
    for (const auto& alt : r.keys) {
        std::string c;
        for (const auto& k : alt) {
            if (!c.empty()) c += "+";
            c += lower(k);
            caps.push_back(lower(k));
        }
        combos.push_back(c);
    }
    auto key_hit = [&](const std::string& w) {
        if (w.find('+') != std::string::npos && w.size() > 1) {
            for (const auto& c : combos)
                if (c == w || c.find(w) == 0) return true;   // "ctrl+shift" is a prefix
            return false;
        }
        for (const auto& k : caps)
            if (k == w) return true;
        return false;
    };
    const std::string hay = lower(r.section + "\n" + r.description + "\n" + r.where);
    bool all_keys = true, all_any = true;
    for (const auto& w : ws) {
        const bool k = key_hit(w);
        const bool t = hay.find(w) != std::string::npos;
        if (!k) all_keys = false;
        if (!k && !t) all_any = false;
    }
    if (all_keys) return 2;
    return all_any ? 1 : 0;
}

std::vector<bool> key_lit_alternatives(const KeyRow& r, const std::string& query) {
    std::vector<bool> out(r.keys.size(), false);
    if (key_row_match(r, query) != 2) return out;
    // Each alternative alone, as a row of its own: lit when the query finds it
    // by its keys.
    for (std::size_t a = 0; a < r.keys.size(); ++a) {
        KeyRow one;
        one.keys = {r.keys[a]};
        out[a] = key_row_match(one, query) == 2;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Collision detection
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> find_accel_collisions() {
    std::map<std::string, std::pair<int, bool>> seen;  // accel -> {count, any_action}
    for (const auto& s : shortcut_registry())
        for (const auto& a : s.accels) {
            auto& e = seen[a];
            e.first += 1;
            if (!s.action.empty()) e.second = true;
        }
    std::vector<std::string> out;
    for (const auto& [accel, e] : seen)
        if (e.first >= 2 && e.second) out.push_back(accel);  // std::map keeps it sorted+unique
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Text-editing chords  (s016c)
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// "<Control>Delete", "<Primary>delete" and "<Ctrl>Delete" are one chord.
// Lower-case everything and fold the three spellings of Control into one.
std::string fold(std::string a) {
    for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* alias : {"<control>", "<primary>"}) {
        for (std::size_t at; (at = a.find(alias)) != std::string::npos;)
            a.replace(at, std::string(alias).size(), "<ctrl>");
    }
    return a;
}

}  // namespace

bool steals_text_editing(const std::string& accel) {
    // What GtkText and GtkTextView bind for editing and moving, plus the
    // clipboard and undo chords every text box honours. A bare printable key
    // is not listed because nothing in jot binds one app-wide.
    static const char* kText[] = {
        "delete", "backspace", "<ctrl>delete", "<ctrl>backspace",
        "<shift>delete", "<shift>insert", "<ctrl>insert",
        "<ctrl>a", "<ctrl>c", "<ctrl>v", "<ctrl>x", "<ctrl>z", "<ctrl><shift>z",
        "<ctrl>y", "home", "end", "<ctrl>home", "<ctrl>end",
        "<ctrl>left", "<ctrl>right", "<ctrl>up", "<ctrl>down",
        "return", "tab", "space",
    };
    const std::string a = fold(accel);
    for (const char* t : kText)
        if (a == t) return true;
    return false;
}

std::vector<std::string> find_text_editing_steals() {
    std::vector<std::string> out;
    for (const auto& s : shortcut_registry())
        if (!s.action.empty())
            for (const auto& a : s.accels)
                if (steals_text_editing(a)) out.push_back(s.action + " " + a);
    return out;
}

}  // namespace jot::core
