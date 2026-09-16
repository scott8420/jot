#include "Keybinding.hpp"

#include "Log.hpp"
#include "core/Hotkey.hpp"

#include <giomm/settings.h>
#include <giomm/settingsschema.h>
#include <giomm/settingsschemakey.h>
#include <giomm/settingsschemasource.h>
#include <glibmm/variant.h>

#include <gio/gio.h>

#include <filesystem>
#include <map>
#include <system_error>

namespace jot::keys {
namespace {

constexpr const char* kMediaKeys  = "org.gnome.settings-daemon.plugins.media-keys";
constexpr const char* kCustomItem = "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding";
constexpr const char* kListKey    = "custom-keybindings";

// THE GUARD. g_settings_new on an uninstalled schema is fatal -- not an
// exception, not a null: the process dies. Every Settings in this file is
// built through here, and the null return is the whole of what "this desktop
// is not GNOME" looks like to the rest of jot.
Glib::RefPtr<Gio::SettingsSchema> schema_for(const char* id) {
    auto src = Gio::SettingsSchemaSource::get_default();
    if (!src) return {};
    return src->lookup(id, true);
}

Glib::RefPtr<Gio::Settings> settings_for(const char* id) {
    if (!schema_for(id)) return {};
    return Gio::Settings::create(id);
}

Glib::RefPtr<Gio::Settings> slot_settings() {
    if (!schema_for(kCustomItem)) return {};
    return Gio::Settings::create(kCustomItem, core::jot_slot_path());
}

std::vector<std::string> read_list(const Glib::RefPtr<Gio::Settings>& s) {
    std::vector<std::string> out;
    if (!s) return out;
    for (const auto& e : s->get_string_array(kListKey)) out.push_back(e.raw());
    return out;
}

bool write_list(const Glib::RefPtr<Gio::Settings>& s,
                const std::vector<std::string>& list) {
    std::vector<Glib::ustring> v;
    v.reserve(list.size());
    for (const auto& e : list) v.emplace_back(e);
    return s->set_string_array(kListKey, v);
}

// dconf writes are queued; a keypress may arrive before the queue drains, and
// a settings write that has not landed is one gnome-settings-daemon has not
// seen. The C call because glibmm does not wrap it.
void flush() { g_settings_sync(); }

// Where a conflicting chord lives, in words. The schema id is the truth and
// the label is for a person, so the map is small and the fallback is the id
// itself rather than a guess.
std::string friendly_schema(const std::string& id) {
    static const std::map<std::string, std::string> kNames = {
        {"org.gnome.desktop.wm.keybindings",              "Windows"},
        {"org.gnome.mutter.keybindings",                  "Windows"},
        {"org.gnome.mutter.wayland.keybindings",          "Windows"},
        {"org.gnome.shell.keybindings",                   "GNOME Shell"},
        {"org.gnome.settings-daemon.plugins.media-keys",  "System shortcuts"},
    };
    auto it = kNames.find(id);
    return it == kNames.end() ? id : it->second;
}

std::string prettify(const std::string& key) {
    std::string s = key;
    for (char& c : s)
        if (c == '-') c = ' ';
    if (!s.empty()) s[0] = char(std::toupper((unsigned char)s[0]));
    return s;
}

// One schema, every key, every string in it. A keybinding key is `as` in the
// window-manager schemas and `s` in a custom slot, and media-keys has both
// shapes plus keys that are not bindings at all -- so the TYPE decides how it
// is read and a chord comparison decides whether it matters. Anything else in
// the schema simply never matches an accel.
void scan_schema(const char* id, const std::string& accel,
                 std::vector<Conflict>& out) {
    auto schema = schema_for(id);
    if (!schema) return;
    auto s = Gio::Settings::create(id);
    for (const auto& key : schema->list_keys()) {
        auto k = schema->get_key(key);
        if (!k) continue;
        const std::string type = k->get_value_type().get_string();
        std::vector<std::string> values;
        if (type == "as") {
            for (const auto& v : s->get_string_array(key)) values.push_back(v.raw());
        } else if (type == "s") {
            values.push_back(s->get_string(key).raw());
        } else {
            continue;
        }
        for (const auto& v : values) {
            if (!core::accels_equal(v, accel)) continue;
            out.push_back({friendly_schema(id), prettify(key.raw())});
            break;   // one row per setting, not one per spelling of the chord
        }
    }
}

}  // namespace

std::string exe_path() {
    std::error_code ec;
    const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.string();
    return "jot";   // honest last resort: $PATH, and the row shows what it will run
}

Status current() {
    Status st;
    auto media = settings_for(kMediaKeys);
    auto slot  = slot_settings();
    if (!media || !slot) return st;      // not GNOME -- supported stays false
    st.supported = true;

    const auto list = read_list(media);
    const bool listed = core::has_slot(list, core::jot_slot_path());
    st.accel   = slot->get_string("binding").raw();
    st.command = slot->get_string("command").raw();
    st.ours    = core::looks_like_capture_command(st.command);
    // LISTED AND BOUND, both. A slot that is listed with an empty binding is
    // not a shortcut, and reporting it as one would leave the user pressing a
    // key that was never grabbed and wondering which half is broken.
    st.installed = listed && !st.accel.empty();
    return st;
}

bool install(const std::string& accel, const std::string& command, std::string* err) {
    auto media = settings_for(kMediaKeys);
    auto slot  = slot_settings();
    if (!media || !slot) {
        if (err) *err = "This desktop doesn't have GNOME's keyboard settings, "
                        "so jot can't set a global shortcut here.";
        return false;
    }
    // Contents first, membership second -- see the header. Name included
    // because it is what GNOME Settings will show in its own list, and an
    // unnamed custom shortcut there is a mystery the user cannot attribute.
    const bool ok = slot->set_string("name", "jot \u2014 capture a thought") &&
                    slot->set_string("command", command) &&
                    slot->set_string("binding", accel);
    if (!ok) {
        if (err) *err = "GNOME refused to store the shortcut.";
        return false;
    }
    if (!write_list(media, core::with_slot(read_list(media), core::jot_slot_path()))) {
        if (err) *err = "The shortcut was stored but GNOME refused to list it.";
        return false;
    }
    flush();
    if (auto lg = log::get(log::Area::App))
        lg->info("capture hotkey installed: {} -> {}", accel, command);
    return true;
}

bool uninstall(std::string* err) {
    auto media = settings_for(kMediaKeys);
    auto slot  = slot_settings();
    if (!media || !slot) {
        if (err) *err = "This desktop doesn't have GNOME's keyboard settings.";
        return false;
    }
    if (!write_list(media, core::without_slot(read_list(media), core::jot_slot_path()))) {
        if (err) *err = "GNOME refused to remove the shortcut.";
        return false;
    }
    // Unlisted first, THEN emptied: a slot that is listed and blank is a live
    // keybinding with nothing behind it. Reset rather than set-to-empty so the
    // path goes back to holding nothing at all.
    slot->reset("binding");
    slot->reset("command");
    slot->reset("name");
    flush();
    if (auto lg = log::get(log::Area::App)) lg->info("capture hotkey removed");
    return true;
}

std::vector<Conflict> conflicts(const std::string& accel) {
    std::vector<Conflict> out;
    if (core::canonical_accel(accel).empty()) return out;

    for (const char* id : {"org.gnome.desktop.wm.keybindings",
                           "org.gnome.mutter.keybindings",
                           "org.gnome.mutter.wayland.keybindings",
                           "org.gnome.shell.keybindings",
                           kMediaKeys})
        scan_schema(id, accel, out);

    // Everyone else's custom shortcuts, ours excluded -- rebinding jot's own
    // key must not report jot as the thing in the way.
    if (auto media = settings_for(kMediaKeys)) {
        const auto mine = core::jot_slot_path();
        for (const auto& path : read_list(media)) {
            if (path == mine || path.empty() || path.back() != '/') continue;
            if (!schema_for(kCustomItem)) break;
            auto s = Gio::Settings::create(kCustomItem, path);
            if (!core::accels_equal(s->get_string("binding").raw(), accel)) continue;
            std::string name = s->get_string("name").raw();
            if (name.empty()) name = s->get_string("command").raw();
            out.push_back({"Another custom shortcut", name});
        }
    }
    return out;
}

}  // namespace jot::keys
