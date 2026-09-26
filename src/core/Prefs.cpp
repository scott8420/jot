#include "core/Prefs.hpp"

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace jot::core {
namespace {

// A stored position has to survive a window that was resized, a monitor that
// changed, and a hand-edited file. Clamping here rather than at the widget
// means one rule instead of one per caller, and a nonsense value in the file
// comes back as a usable pane rather than a pane with no width.
int sane(int v, int lo, int hi, int fallback) {
    if (v < lo || v > hi) return fallback;
    return v;
}

template <class T>
T get_or(const nlohmann::json& j, const char* key, T fallback) {
    if (!j.contains(key)) return fallback;
    try {
        return j.at(key).get<T>();
    } catch (const std::exception&) {
        return fallback;   // wrong type in the file -- take the default, say nothing
    }
}

}  // namespace

Prefs load_prefs(const std::string& file) {
    Prefs p;
    std::ifstream f(file);
    if (!f) return p;                              // first run -- defaults
    try {
        nlohmann::json j;
        f >> j;
        p.show_tree   = get_or(j, "show_tree", p.show_tree);
        p.show_drawer = get_or(j, "show_drawer", p.show_drawer);
        p.reading     = get_or(j, "reading", p.reading);
        p.live_preview = get_or(j, "live_preview", p.live_preview);
        p.tree_width  = sane(get_or(j, "tree_width", p.tree_width), 120, 2000, 280);
        p.note_width  = sane(get_or(j, "note_width", p.note_width), 200, 4000, 560);
        p.desktop_tasks = get_or(j, "desktop_tasks", p.desktop_tasks);
        // Clamped like the pane widths, and for a sharper reason: a stored size
        // can outlive the monitor it was stored on. A window wider than any
        // attached display is one you cannot reach the edges of to fix.
        p.win_width     = sane(get_or(j, "win_width",  p.win_width),  400, 20000, 940);
        p.win_height    = sane(get_or(j, "win_height", p.win_height), 300, 20000, 620);
        p.win_maximized = get_or(j, "win_maximized", p.win_maximized);
        p.notify_due    = get_or(j, "notify_due", p.notify_due);
        p.background    = get_or(j, "background", p.background);
        p.drop_links    = get_or(j, "drop_links", p.drop_links);
        // A list, not a scalar, so get_or's type deduction does not apply --
        // and a malformed entry must not take the whole prefs file down with
        // it, which is why the element type is checked rather than assumed.
        if (auto it = j.find("announced"); it != j.end() && it->is_array()) {
            p.announced.clear();
            for (const auto& e : *it)
                if (e.is_string()) p.announced.push_back(e.get<std::string>());
        }
        // An object of bools. Same tolerance as `announced`: a wrong-typed value
        // drops that one entry, and a wrong-typed container drops the lot to
        // defaults, never the file.
        if (auto it = j.find("drawer_open"); it != j.end() && it->is_object()) {
            p.drawer_open.clear();
            for (auto e = it->begin(); e != it->end(); ++e)
                if (e.value().is_boolean()) p.drawer_open[e.key()] = e.value().get<bool>();
        }
    } catch (const std::exception&) {
        return Prefs{};                            // unparseable -- defaults, never throw
    }
    return p;
}

bool save_prefs(const std::string& file, const Prefs& p) {
    try {
        fs::create_directories(fs::path(file).parent_path());
    } catch (const std::exception&) {
        return false;
    }
    nlohmann::json j;
    j["show_tree"]   = p.show_tree;
    j["show_drawer"] = p.show_drawer;
    j["reading"]     = p.reading;
    j["live_preview"] = p.live_preview;
    j["tree_width"]  = p.tree_width;
    j["note_width"]  = p.note_width;
    j["desktop_tasks"] = p.desktop_tasks;
    j["win_width"]     = p.win_width;
    j["win_height"]    = p.win_height;
    j["win_maximized"] = p.win_maximized;
    j["notify_due"]    = p.notify_due;
    j["background"]    = p.background;
    j["drop_links"]    = p.drop_links;
    j["announced"]     = p.announced;
    j["drawer_open"]   = p.drawer_open;
    std::ofstream f(file);
    if (!f) return false;
    f << j.dump(2) << "\n";
    return static_cast<bool>(f);
}

}  // namespace jot::core
