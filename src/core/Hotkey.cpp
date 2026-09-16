#include "core/Hotkey.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace jot::core {
namespace {

std::string lower(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) o += char(std::tolower((unsigned char)c));
    return o;
}

// The keys that must not become a global grab no matter what is held down with
// them. Escape and Return are how a person gets OUT of the dialog that is
// asking, and Tab is how the desktop is navigated; a global grab on any of them
// is a machine you have to log out of to repair.
bool forbidden_key(const std::string& key) {
    static const std::set<std::string> kNo = {
        "Escape", "Return", "KP_Enter", "Tab", "ISO_Left_Tab", "space",
        "BackSpace", "Delete", "Print", "Menu",
    };
    return kNo.count(key) > 0;
}

bool is_function_key(const std::string& key) {
    if (key.size() < 2 || key[0] != 'F') return false;
    for (std::size_t i = 1; i < key.size(); ++i)
        if (!std::isdigit((unsigned char)key[i])) return false;
    return true;
}

}  // namespace

AccelParts parse_accel_parts(const std::string& accel) {
    AccelParts p;
    std::size_t i = 0;
    while (i < accel.size()) {
        if (accel[i] == '<') {
            const std::size_t j = accel.find('>', i);
            if (j == std::string::npos) break;    // "<Contro" -- no key, no parts
            const std::string m = lower(accel.substr(i + 1, j - i - 1));
            if (m == "ctrl" || m == "control" || m == "primary")            p.ctrl = true;
            else if (m == "alt" || m == "mod1")                             p.alt = true;
            else if (m == "shift")                                          p.shift = true;
            else if (m == "super" || m == "meta" || m == "mod4" || m == "hyper")
                p.super = true;
            // An unknown modifier is IGNORED rather than fatal: dconf can hold
            // <Mod5> and friends, and dropping the chord entirely would report
            // "no conflict" about a chord that is very much taken.
            i = j + 1;
        } else {
            p.key = accel.substr(i);
            break;
        }
    }
    // GTK spells single-character keys lowercase ("<Control>j"); dconf and hand
    // edits are not so careful. Multi-character NAMES are case-sensitive and
    // must not be touched -- "F5" is not "f5" and "Left" is not "left".
    if (p.key.size() == 1) p.key[0] = char(std::tolower((unsigned char)p.key[0]));
    return p;
}

std::string canonical_accel(const std::string& accel) {
    const AccelParts p = parse_accel_parts(accel);
    if (p.key.empty()) return "";
    std::string out;
    if (p.ctrl)  out += "<Control>";
    if (p.alt)   out += "<Alt>";
    if (p.shift) out += "<Shift>";
    if (p.super) out += "<Super>";
    out += p.key;
    return out;
}

bool accels_equal(const std::string& a, const std::string& b) {
    const std::string ca = canonical_accel(a);
    if (ca.empty()) return false;               // nothing equals nonsense
    return ca == canonical_accel(b);
}

std::string hotkey_objection(const std::string& accel) {
    const AccelParts p = parse_accel_parts(accel);
    if (p.key.empty())
        return "That isn't a key combination jot can use.";
    if (forbidden_key(p.key))
        return "The desktop needs that key. Pick another one.";
    // A function key stands alone because nothing else on the desktop types
    // one: F12 grabbed globally costs you F12 and nothing more. Every other key
    // is a letter somebody is in the middle of typing.
    if (!p.qualifying_modifier() && !is_function_key(p.key))
        return "A global shortcut needs Ctrl, Alt or Super in it \u2014 "
               "otherwise it would swallow that key everywhere on the desktop.";
    return "";
}

const char* custom_keybindings_prefix() {
    return "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/";
}

std::string jot_slot_path() {
    // THE TRAILING SLASH IS LOAD-BEARING. A relocatable GSettings path must end
    // in one, and gnome-settings-daemon silently ignores an entry that does not
    // -- which looks exactly like "the hotkey did not take".
    return std::string(custom_keybindings_prefix()) + "jot-capture/";
}

bool has_slot(const std::vector<std::string>& list, const std::string& path) {
    return std::find(list.begin(), list.end(), path) != list.end();
}

std::vector<std::string> with_slot(const std::vector<std::string>& list,
                                   const std::string& path) {
    std::vector<std::string> out;
    out.reserve(list.size() + 1);
    bool kept = false;
    for (const auto& e : list) {
        if (e == path) {
            if (kept) continue;            // a duplicate of OURS -- drop the second
            kept = true;
        }
        out.push_back(e);                  // a stranger's entry, verbatim, in place
    }
    if (!kept) out.push_back(path);
    return out;
}

std::vector<std::string> without_slot(const std::vector<std::string>& list,
                                      const std::string& path) {
    std::vector<std::string> out;
    out.reserve(list.size());
    for (const auto& e : list)
        if (e != path) out.push_back(e);
    return out;
}

std::string capture_command(const std::string& exe) {
    std::string cmd = exe;
    // Quote only when it is needed, because the unquoted form is the one a
    // person can read in the preferences row and paste into a terminal.
    if (cmd.find_first_of(" \t'\"\\$`") != std::string::npos) {
        std::string q = "'";
        for (char c : cmd) {
            if (c == '\'') q += "'\\''";    // close, escape, reopen
            else q += c;
        }
        q += "'";
        cmd = q;
    }
    return cmd + " --capture";
}

bool looks_like_capture_command(const std::string& command) {
    if (command.find("--capture") == std::string::npos) return false;
    // "jot" has to appear in the program part, or a stranger's slot that
    // happens to pass --capture to something else would read as ours.
    const std::size_t flag = command.find("--capture");
    return command.substr(0, flag).find("jot") != std::string::npos;
}

}  // namespace jot::core
