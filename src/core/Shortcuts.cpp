#include "core/Shortcuts.hpp"

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
        {"slash", "/"},     {"minus", "\u2212"}, {"plus", "+"},
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
const std::vector<ShortcutSpec>& shortcut_registry() {
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
        {"General", "win.save-as",       {"<Ctrl><Shift>s"}, "",
         "Write a second jots folder with everything in it, and switch to it"},
        {"General", "win.save-all",      {"<Ctrl>s"}, "",
         "Save everything now (structure already saves itself)"},
        {"General", "win.shortcuts",     {"<Ctrl>question", "<Ctrl>slash"}, "",
         "Keyboard shortcuts (this window)"},
        {"General", "win.about",         {"F1"}, "", "About jot"},
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
        {"Notes", "win.new-child",      {"<Ctrl><Shift>n"}, "",
         "New note under the selected one"},
        {"Notes", "win.delete-note",    {"<Ctrl>Delete"}, "",
         "Delete the selected note and everything under it"},
        {"Notes", "win.toggle-protect", {"<Ctrl>l"}, "",
         "Protect or unprotect the selected note"},
        {"Notes", "win.rename-note",    {"F2"}, "",
         "Rename the selected note in the tree"},

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

        // ── View ──────────────────────────────────────────────
        // Both off is the focus mode: the note alone on screen.
        {"View", "win.toggle-tree",     {"F9"}, "",
         "Show or hide the side pane (Notes and Today)"},
        {"View", "win.toggle-drawer",   {"F10"}, "", "Show or hide note details"},
    };
    return kReg;
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

}  // namespace jot::core
