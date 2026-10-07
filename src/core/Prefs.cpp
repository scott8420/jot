#include "core/Prefs.hpp"
#include "core/Zoom.hpp"
#include "core/RowLook.hpp"

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
        p.accent        = get_or(j, "accent", p.accent);                 // s050b
        if (!p.accent.empty() && !is_hex_colour(p.accent)) p.accent.clear();
        p.zoom          = zoom_clamp(get_or(j, "zoom", p.zoom));         // s053b
        // A list, not a scalar, so get_or's type deduction does not apply --
        // and a malformed entry must not take the whole prefs file down with
        // it, which is why the element type is checked rather than assumed.
        if (auto it = j.find("announced"); it != j.end() && it->is_array()) {
            p.announced.clear();
            for (const auto& e : *it)
                if (e.is_string()) p.announced.push_back(e.get<std::string>());
        }
        // s041: [{"key": "...", "until": N}, ...] -- same tolerance.
        if (auto it = j.find("snoozed"); it != j.end() && it->is_array()) {
            p.snoozed.clear();
            for (const auto& e : *it)
                if (e.is_object() && e.contains("key") && e["key"].is_string() &&
                    e.contains("until") && e["until"].is_number_integer())
                    p.snoozed.push_back(
                        {e["key"].get<std::string>(), e["until"].get<std::int64_t>()});
        }
        // An object of bools. Same tolerance as `announced`: a wrong-typed value
        // drops that one entry, and a wrong-typed container drops the lot to
        // defaults, never the file.
        if (auto it = j.find("drawer_open"); it != j.end() && it->is_object()) {
            p.drawer_open.clear();
            for (auto e = it->begin(); e != it->end(); ++e)
                if (e.value().is_boolean()) p.drawer_open[e.key()] = e.value().get<bool>();
        }
        // An object of string arrays; same tolerance -- a wrong-typed entry drops.
        if (auto it = j.find("move_recent"); it != j.end() && it->is_object()) {
            p.move_recent.clear();
            for (auto e = it->begin(); e != it->end(); ++e) {
                if (!e.value().is_array()) continue;
                auto& list = p.move_recent[e.key()];
                for (const auto& id : e.value())
                    if (id.is_string()) list.push_back(id.get<std::string>());
            }
        }
        // s038b. An array of {name, query}; an entry missing either drops.
        if (auto it = j.find("perspectives"); it != j.end() && it->is_array()) {
            p.perspectives.clear();
            for (const auto& e : *it) {
                if (!e.is_object()) continue;
                auto n = e.find("name");
                auto q = e.find("query");
                if (n == e.end() || q == e.end() || !n->is_string() || !q->is_string()) continue;
                p.perspectives.push_back({n->get<std::string>(), q->get<std::string>()});
            }
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
    j["accent"]        = p.accent;   // s050b
    j["zoom"]          = p.zoom;     // s053b
    j["announced"]     = p.announced;
    j["snoozed"]       = nlohmann::json::array();
    for (const auto& z : p.snoozed)
        j["snoozed"].push_back({{"key", z.key}, {"until", z.until}});
    j["drawer_open"]   = p.drawer_open;
    j["move_recent"]   = p.move_recent;
    j["perspectives"]  = nlohmann::json::array();
    for (const auto& v : p.perspectives)
        j["perspectives"].push_back({{"name", v.name}, {"query", v.query}});
    std::ofstream f(file);
    if (!f) return false;
    f << j.dump(2) << "\n";
    return static_cast<bool>(f);
}

}  // namespace jot::core
