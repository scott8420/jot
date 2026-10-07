#include <string>
#include "Appearance.hpp"
#include "Log.hpp"
#include "core/RowLook.hpp"

#include <gtkmm/settings.h>

// The D-Bus work is the GIO C API directly rather than gtkmm's wrappers around
// GDBusProxy, which vary across 4.x point releases. This is plumbing whose
// entire job is to compile on the first try against whatever gtkmm the machine
// has, and it is the one file in Slate that talks to the session bus.
#include <gio/gio.h>

#include <gtkmm/cssprovider.h>
#include <gtkmm/stylecontext.h>
#include <gdkmm/display.h>

namespace jot::appearance {


namespace {

// The tri-state the portal speaks. Named rather than left as bare integers
// because `2` appearing in a comparison is exactly the kind of thing that gets
// "simplified" into `!= 1` by a later reader, which silently turns "prefer
// light" into "no preference".
enum class Scheme { NoPreference = 0, PreferDark = 1, PreferLight = 2 };

const char* scheme_name(Scheme s) {
    switch (s) {
        case Scheme::NoPreference: return "no-preference";
        case Scheme::PreferDark:   return "prefer-dark";
        case Scheme::PreferLight:  return "prefer-light";
    }
    return "?";
}

// Apply, or deliberately decline to. `no-preference` leaves GTK's own default
// alone: the desktop said nothing, and overriding a setting the user may have
// made elsewhere (GTK_THEME, a settings.ini) on the strength of silence would
// be this app inventing an opinion it was not given.
// ── jot's own stylesheet, and the only rule in it ──────────────────────────
// GTK's stock `.error` class is ONE colour for both schemes -- a red picked to
// carry on a light background, which on a dark one stops reading as "late" and
// starts reading as "alarm". So the overdue line gets jot's own class and this
// file, which already knows which way the desktop is leaning, picks the value.
//
// It lives HERE rather than next to the widget that wears it because the answer
// changes when the DESKTOP changes, not when the pane rebuilds: apply() is
// called again on every scheme switch, so a live toggle repaints for free. A
// provider installed at the display, loaded twice, never accumulating.
Glib::RefPtr<Gtk::CssProvider> g_css;

// ── s042 (J1): the cards ───────────────────────────────────────────────────
// Colour that carries MEANING, one hue per core::RowState, each picked twice:
// a light-scheme value that reads on white and a dark-scheme value that reads
// on charcoal without shouting. The greys (on hold / waiting / deferred) are
// the text colour at low alpha, so they follow whatever theme is in force.
// Surfaces are alpha(currentColor, ...) for the same reason: a card is a tint
// of the page, not a colour jot invented.
struct Palette { const char *overdue, *today, *flagged, *available, *done; };
constexpr Palette kLight{"#c01c28", "#c64600", "#9c6e00", "#1c71d8", "#26a269"};
constexpr Palette kDark {"#ff7b9c", "#ffa348", "#f6d32d", "#78aeed", "#57e389"};

// s050: the accent. GTK without libadwaita (s004) has no name for it that
// holds across versions -- 4.14 has none, and a CSS colour that does not
// resolve paints NOTHING (the selection vanished under Xvfb). So jot asks the
// settings portal itself (accent-color, GNOME 47+), live like dark mode, and
// falls back to GNOME's blue.
std::string g_accent = jot::core::kDefaultAccent;   // what the DESKTOP says
std::string g_chosen;            // s050b: Preferences' pick; "" = follow the desktop
bool        g_dark   = false;   // the scheme last applied, for an accent-only repaint
std::string accent_ref() { return g_chosen.empty() ? g_accent : g_chosen; }

std::string sheet(bool dark) {
    const Palette& p = dark ? kDark : kLight;
    std::string c;
    c += std::string(".jot-overdue { color: ") + p.overdue + "; }\n";
    c += ".jot-card { border-radius: 10px; padding: 2px 6px 2px 8px; "
         "background-color: alpha(currentColor, 0.05); "
         "border-left: 4px solid alpha(currentColor, 0.22); }\n";
    c += ".jot-card:hover { background-color: alpha(currentColor, 0.09); }\n";
    c += ".jot-card-dim { opacity: 0.7; }\n";
    // s043: a view's head -- big, light, and close to its summary line.
    c += ".jot-view-title { font-size: 1.6em; font-weight: 800; }\n";
    // s043: a note card (Find, Inbox) -- the card shape with a quiet edge.
    c += ".jot-card.st-note { border-left-color: alpha(currentColor, 0.12); }\n";
    c += ".jot-card.st-inbox { border-left-color: alpha(currentColor, 0.45); }\n";
    c += ".jot-card-go { padding: 5px 2px; min-height: 0; background: none; box-shadow: none; }\n";
    c += ".jot-card-go:hover, .jot-card-go:active { background: none; }\n";
    c += ".jot-card-title { font-weight: 500; }\n";
    c += ".jot-card.st-done .jot-card-title { text-decoration-line: line-through; opacity: 0.6; }\n";
    c += ".jot-tick check { border-radius: 50%; min-width: 16px; min-height: 16px; }\n";
    c += ".jot-chip { border-radius: 999px; padding: 0 8px; font-size: smaller; "
         "background-color: alpha(currentColor, 0.08); }\n";
    c += ".jot-project { color: alpha(currentColor, 0.75); }\n";
    // s048: the outline. A todo row is a one-line card inside the sidebar's
    // own rounded selection; a project is a heading with its steps counted.
    c += ".jot-tree row { border-radius: 8px; }\n";
    c += ".jot-tree-card { padding: 3px 8px 3px 4px; min-height: 26px; }\n";
    c += ".jot-tree row:selected .jot-tree-card { background-color: alpha(currentColor, 0.04); }\n";
    c += ".jot-tree-project { font-weight: 700; }\n";
    // ── s050: the Mac look pass ────────────────────────────────────────────
    // Air: a little space between rows, so cards read as cards, not a stack.
    c += ".jot-tree row { margin-top: 1px; margin-bottom: 1px; }\n";
    // The selection in the ACCENT colour, as a source list does -- a tint,
    // not a slab, so the cards' stripes still read on a selected row. GTK
    // 4.16 moved its theme colours to CSS variables; before that they are
    // named colours. Asked at run time, so one binary is right on both.
    const std::string accent = accent_ref();
    c += ".jot-tree row:selected { background-color: alpha(" + accent + ", 0.20); }\n";
    c += ".jot-tree row:selected:hover { background-color: alpha(" + accent + ", 0.26); }\n";
    c += ".jot-tree row:selected .jot-tree-card { background-color: alpha(" + accent + ", 0.06); }\n";
    // s050b: and the keyboard ring, or a chosen orange wears a blue outline.
    c += ".jot-tree row:focus-visible { outline-color: alpha(" + accent + ", 0.8); }\n";
    // The side pane a shade off the editor -- a Mac source list. A tint of
    // the text colour: darker on a light theme, a touch lighter on a dark one
    // (the way macOS does it in dark mode). Its lists go transparent so the
    // shade is the pane's, not a white sheet laid over it.
    c += ".jot-side { background-color: alpha(currentColor, 0.045); }\n";
    c += ".jot-side list, .jot-side scrolledwindow, .jot-side viewport "
         "{ background-color: transparent; }\n";
    // A tick, once: the card flashes the done colour and the title fades to
    // its struck-through dim. Only on the row just ticked (TreePane marks it
    // jot-just-done); GTK skips animations when the desktop turns them off.
    c += std::string("@keyframes jot-done-flash { from { background-color: alpha(") + p.done +
         ", 0.35); } to { background-color: alpha(currentColor, 0.05); } }\n";
    c += "@keyframes jot-done-fade { from { opacity: 1; } to { opacity: 0.55; } }\n";
    c += ".jot-tree-card.jot-just-done { animation: jot-done-flash 500ms ease-out; }\n";
    c += ".jot-just-done .jot-tree-title { animation: jot-done-fade 500ms ease-out; }\n";
    c += ".jot-count { color: alpha(currentColor, 0.7); font-weight: 600; }\n";
    c += std::string(".jot-count.jot-count-met { color: ") + p.done + "; }\n";   // s054: ready to tick
    // s055: a deadline's mark and its pace line -- quiet on track, the
    // today colour when running short, the late colour when late.
    c += std::string(".jot-pace-short { color: ") + p.today + "; }\n";
    c += std::string(".jot-pace-late { color: ") + p.overdue + "; }\n";
    c += std::string(".jot-pace-ready { color: ") + p.done + "; }\n";
    c += std::string(".jot-due.jot-pace-short { background-color: alpha(") + p.today + ", 0.16); }\n";
    c += std::string(".jot-due.jot-pace-late { background-color: alpha(") + p.overdue + ", 0.16); }\n";
    c += std::string(".jot-flag { color: ") + p.flagged + "; }\n";
    // s044: a packet's items -- in is green and quiet, missing is the text.
    c += std::string(".jot-packet-in { color: ") + p.done + "; }\n";
    c += ".jot-packet-missing { font-weight: 600; }\n";
    c += std::string(".jot-packet-done { color: ") + p.done + "; font-weight: 600; }\n";
    c += std::string(".jot-packet-sent { color: ") + p.done + "; }\n";   // s051

    const auto state = [&](const char* st, const char* col, bool chip) {
        c += std::string(".jot-card.") + st + " { border-left-color: " + col + "; }\n";
        c += std::string(".jot-card.") + st + " .jot-tick check { border-color: " + col + "; }\n";
        if (chip)
            c += std::string(".jot-due.") + st + " { color: " + col +
                 "; background-color: alpha(" + col + ", 0.16); }\n";
    };
    state("st-overdue",   p.overdue,   true);
    state("st-today",     p.today,     true);
    state("st-flagged",   p.flagged,   false);
    state("st-available", p.available, false);
    state("st-done",      p.done,      false);
    c += std::string(".jot-tick check:checked { background-image: none; background-color: ") + p.done +
         "; border-color: " + p.done + "; }\n";
    return c;
}

void apply_css(bool dark) {
    auto display = Gdk::Display::get_default();
    if (!display) return;
    if (!g_css) {
        g_css = Gtk::CssProvider::create();
        Gtk::StyleContext::add_provider_for_display(
            display, g_css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_dark = dark;
    g_css->load_from_data(sheet(dark));
    if (auto lg = log::get(log::Area::App))
        lg->info("appearance: stylesheet {} with accent {} ({})", dark ? "dark" : "light", accent_ref(),
                 g_chosen.empty() ? "the desktop's" : "chosen in Preferences");
}

void apply(Scheme s) {
    auto lg = log::get(log::Area::App);
    auto settings = Gtk::Settings::get_default();

    if (s == Scheme::NoPreference) {
        // The desktop declined to say, so GTK's own default stands -- but the
        // stylesheet still has to pick a side, and the side it picks is
        // whatever GTK is actually doing rather than an assumption of light.
        if (lg) lg->info("appearance: no preference expressed; leaving GTK's default");
        apply_css(settings && settings->property_gtk_application_prefer_dark_theme());
        return;
    }

    if (!settings) {
        if (lg) lg->error("appearance: no Gtk::Settings; cannot apply {}", scheme_name(s));
        return;
    }

    const bool dark = (s == Scheme::PreferDark);
    settings->property_gtk_application_prefer_dark_theme() = dark;
    apply_css(dark);
    if (lg) lg->info("appearance: applied {} (prefer_dark={})", scheme_name(s), dark);
}

// The portal hands back a value wrapped in a variant, and HOW MANY layers of
// wrapping depends on which method answered: the older Read() returns a variant
// holding a variant holding the number, the newer ReadOne() returns the number
// directly. Rather than encode that difference at each call site -- which is
// how a working app breaks against a portal version nobody tested -- peel until
// something that is not a variant is reached.
bool unwrap_u32(GVariant* v, guint32& out) {
    if (!v) return false;
    GVariant* cur = g_variant_ref(v);
    for (int depth = 0; depth < 8; ++depth) {
        if (g_variant_is_of_type(cur, G_VARIANT_TYPE_VARIANT)) {
            GVariant* inner = g_variant_get_variant(cur);
            g_variant_unref(cur);
            cur = inner;
            continue;
        }
        if (g_variant_is_of_type(cur, G_VARIANT_TYPE_UINT32)) {
            out = g_variant_get_uint32(cur);
            g_variant_unref(cur);
            return true;
        }
        // A tuple is what a method reply arrives as; step into its first child.
        if (g_variant_is_of_type(cur, G_VARIANT_TYPE_TUPLE) &&
            g_variant_n_children(cur) > 0) {
            GVariant* inner = g_variant_get_child_value(cur, 0);
            g_variant_unref(cur);
            cur = inner;
            continue;
        }
        break;
    }
    g_variant_unref(cur);
    return false;
}

// s050: (ddd) somewhere inside however many variant / tuple layers -> hex.
bool unwrap_accent(GVariant* v, std::string& out) {
    if (!v) return false;
    GVariant* cur = g_variant_ref(v);
    for (int depth = 0; depth < 8; ++depth) {
        if (g_variant_is_of_type(cur, G_VARIANT_TYPE("(ddd)"))) {
            double r = -1, g = -1, b = -1;
            g_variant_get(cur, "(ddd)", &r, &g, &b);
            g_variant_unref(cur);
            out = jot::core::accent_css(r, g, b);
            return true;
        }
        if (g_variant_is_of_type(cur, G_VARIANT_TYPE_VARIANT)) {
            GVariant* inner = g_variant_get_variant(cur);
            g_variant_unref(cur);
            cur = inner;
            continue;
        }
        if (g_variant_is_of_type(cur, G_VARIANT_TYPE_TUPLE) && g_variant_n_children(cur) > 0) {
            GVariant* inner = g_variant_get_child_value(cur, 0);
            g_variant_unref(cur);
            cur = inner;
            continue;
        }
        break;
    }
    g_variant_unref(cur);
    return false;
}

void set_accent(const std::string& hex) {
    const std::string want = hex.empty() ? std::string(jot::core::kDefaultAccent) : hex;
    if (auto lg = log::get(log::Area::App))
        lg->info("appearance: accent {}{}", want, hex.empty() ? " (none set; GNOME blue)" : "");
    if (want == g_accent) return;
    g_accent = want;
    if (g_css && g_chosen.empty()) apply_css(g_dark);   // a chosen colour stands
}

// Live updates. The desktop's dark toggle is a switch a person flips while
// looking at the screen, so an app that only reads at startup is correct
// exactly until the moment the user tests it.
void on_setting_changed(GDBusProxy*, const gchar*, const gchar* signal_name,
                        GVariant* parameters, gpointer) {
    if (g_strcmp0(signal_name, "SettingChanged") != 0) return;
    if (g_variant_n_children(parameters) < 3) return;

    const gchar* ns  = nullptr;
    const gchar* key = nullptr;
    g_variant_get_child(parameters, 0, "&s", &ns);
    g_variant_get_child(parameters, 1, "&s", &key);
    if (g_strcmp0(ns, "org.freedesktop.appearance") != 0) return;
    if (g_strcmp0(key, "accent-color") == 0) {   // s050
        GVariant* value = g_variant_get_child_value(parameters, 2);
        std::string hex;
        if (unwrap_accent(value, hex)) set_accent(hex);
        g_variant_unref(value);
        return;
    }
    if (g_strcmp0(key, "color-scheme") != 0) return;

    GVariant* value = g_variant_get_child_value(parameters, 2);
    guint32   raw   = 0;
    if (unwrap_u32(value, raw) && raw <= 2) apply(static_cast<Scheme>(raw));
    g_variant_unref(value);
}

// Returns true if the portal answered -- whatever it answered. A portal that
// says "no preference" HAS answered, and falling through to GSettings after it
// would let GNOME's private key override the cross-desktop one.
bool try_portal() {
    auto lg = log::get(log::Area::App);

    GError* err = nullptr;
    // Held for the process lifetime on purpose: the proxy is what the
    // SettingChanged subscription lives on, so releasing it would silently
    // reduce this to a startup-only read. Never unref'd, and the leak is one
    // object that dies with the process.
    GDBusProxy* proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, nullptr,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings", nullptr, &err);

    if (!proxy) {
        if (lg) lg->info("appearance: no settings portal ({})",
                         err ? err->message : "no detail");
        g_clear_error(&err);
        return false;
    }

    // ReadOne first: Read is deprecated and, on some portal versions, answers
    // with an extra layer of variant. Both are tried because the newer method
    // is simply absent on older portals, which surfaces as an
    // UNKNOWN_METHOD error rather than as anything a version check could see
    // from here.
    GVariant* args = g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme");
    g_variant_ref_sink(args);
    GVariant* reply = g_dbus_proxy_call_sync(
        proxy, "ReadOne", args, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &err);

    if (!reply) {
        g_clear_error(&err);
        reply = g_dbus_proxy_call_sync(
            proxy, "Read", args, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &err);
    }
    g_variant_unref(args);

    if (!reply) {
        if (lg) lg->info("appearance: portal present but color-scheme unreadable ({})",
                         err ? err->message : "no detail");
        g_clear_error(&err);
        return false;
    }

    guint32 raw = 0;
    const bool got = unwrap_u32(reply, raw);
    g_variant_unref(reply);

    if (!got || raw > 2) {
        if (lg) lg->warn("appearance: portal returned an unusable color-scheme");
        return false;
    }

    if (lg) lg->info("appearance: portal says {}", scheme_name(static_cast<Scheme>(raw)));
    // s050: the accent first, so the one stylesheet load below carries it.
    {
        GVariant* a = g_variant_new("(ss)", "org.freedesktop.appearance", "accent-color");
        g_variant_ref_sink(a);
        GVariant* r = g_dbus_proxy_call_sync(proxy, "ReadOne", a, G_DBUS_CALL_FLAGS_NONE, 1000,
                                             nullptr, nullptr);
        if (!r) r = g_dbus_proxy_call_sync(proxy, "Read", a, G_DBUS_CALL_FLAGS_NONE, 1000,
                                           nullptr, nullptr);
        g_variant_unref(a);
        std::string hex;
        if (r && unwrap_accent(r, hex)) g_accent = hex.empty() ? std::string(jot::core::kDefaultAccent) : hex;
        if (lg) lg->info("appearance: accent {}{}", g_accent, r ? "" : " (portal has none; GNOME blue)");
        if (r) g_variant_unref(r);
    }
    apply(static_cast<Scheme>(raw));

    // Only now subscribe. Subscribing before the read would be a race the
    // wrong way round -- a change arriving between the two would be applied and
    // then overwritten by the stale startup value.
    g_signal_connect(proxy, "g-signal", G_CALLBACK(on_setting_changed), nullptr);
    return true;
}

// GNOME's private key, used only when no portal answered.
//
// The schema is looked up before it is opened. g_settings_new() on a schema
// that is not installed does not return null -- it ABORTS THE PROCESS. So the
// obvious two-line version of this function turns "no GNOME schemas installed"
// into a crash on startup, which is a strictly worse outcome than the light
// window it was written to fix.
void try_gsettings() {
    auto lg = log::get(log::Area::App);

    GSettingsSchemaSource* src = g_settings_schema_source_get_default();
    if (!src) {
        if (lg) lg->info("appearance: no GSettings schema source; leaving GTK's default");
        return;
    }

    GSettingsSchema* schema =
        g_settings_schema_source_lookup(src, "org.gnome.desktop.interface", TRUE);
    if (!schema) {
        if (lg) lg->info("appearance: org.gnome.desktop.interface not installed; "
                         "leaving GTK's default");
        return;
    }

    const bool has_key = g_settings_schema_has_key(schema, "color-scheme");
    if (!has_key) {
        if (lg) lg->info("appearance: schema present but has no color-scheme key");
        g_settings_schema_unref(schema);
        return;
    }

    GSettings*  s     = g_settings_new_full(schema, nullptr, nullptr);
    gchar*      value = g_settings_get_string(s, "color-scheme");
    const Scheme sch  = g_strcmp0(value, "prefer-dark") == 0  ? Scheme::PreferDark
                      : g_strcmp0(value, "prefer-light") == 0 ? Scheme::PreferLight
                                                              : Scheme::NoPreference;
    if (lg) lg->info("appearance: gsettings says '{}'", value ? value : "(null)");
    apply(sch);

    g_free(value);
    g_settings_schema_unref(schema);
    // `s` is deliberately kept: it carries the change subscription below.
    g_signal_connect(s, "changed::color-scheme",
                     G_CALLBACK(+[](GSettings* gs, const gchar*, gpointer) {
                         gchar* v = g_settings_get_string(gs, "color-scheme");
                         apply(g_strcmp0(v, "prefer-dark") == 0  ? Scheme::PreferDark
                             : g_strcmp0(v, "prefer-light") == 0 ? Scheme::PreferLight
                                                                 : Scheme::NoPreference);
                         g_free(v);
                     }),
                     nullptr);
}

}  // namespace

void follow_system() {
    if (!try_portal()) try_gsettings();
}

// s050b. Preferences' highlight colour: "#rrggbb", or "" to follow the desktop.
void set_chosen_accent(const std::string& hex) {
    const std::string want = jot::core::is_hex_colour(hex) ? hex : std::string();
    if (want == g_chosen) return;
    g_chosen = want;
    if (g_css) apply_css(g_dark);
}

std::string desktop_accent() { return g_accent; }

}  // namespace jot::appearance
