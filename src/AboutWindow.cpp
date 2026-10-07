#include "AboutWindow.hpp"
#include "widgets/Widgets.hpp"
#include "Registry.hpp"
#include "Log.hpp"

#include <glibmm/markup.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/enums.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/version.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <string>

#ifndef JOT_VERSION
#define JOT_VERSION "0.0.0"
#endif

namespace jot {
namespace {

// One constant: where the code lives. The app id (io.github.scott8420.Jot)
// names the account; the repository name is jot's own.
constexpr const char* kRepoUrl = "https://github.com/scott8420/jot";

void install_about_css() {
    static bool done = false;
    if (done) return;
    auto display = Gdk::Display::get_default();
    if (!display) return;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        // The pages: air, and one grouped card per list, the Mac way.
        ".jot-about-card { background-color: alpha(currentColor, 0.045);"
        "  border-radius: 12px; padding: 4px 14px; }"
        ".jot-about-row { padding: 9px 0; }"
        ".jot-about-row-sep { background-color: alpha(currentColor, 0.08);"
        "  min-height: 1px; }"
        ".jot-about-row-title { font-weight: 600; }"
        ".jot-about-head { font-size: 0.78em; font-weight: 700;"
        "  letter-spacing: 0.06em; opacity: 0.55; margin: 12px 4px 4px 4px; }"
        ".jot-about-version { font-size: 0.9em; opacity: 0.6; }"
        ".jot-about-tagline { font-style: italic; opacity: 0.75; }"
        ".jot-about-licence { font-family: monospace; font-size: 0.86em; }"
        ".jot-about-quiet { font-size: 0.86em; opacity: 0.55; }");
    gtk_style_context_add_provider_for_display(
        display->gobj(), GTK_STYLE_PROVIDER(css->gobj()),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    done = true;
}

widgets::Label* text(const std::string& name, const std::string& markup,
                     int max_chars = 46) {
    auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered, name);
    l->set_markup(markup);
    l->set_wrap(true);
    l->set_xalign(0.0f);
    l->set_max_width_chars(max_chars);
    return l;
}

// A small-caps section heading over a card.
widgets::Label* head(const std::string& name, const std::string& words) {
    auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered, name, words);
    l->set_xalign(0.0f);
    l->add_css_class("jot-about-head");
    return l;
}

widgets::Box* card(const std::string& name) {
    auto* b = Gtk::make_managed<widgets::Box>(widgets::unregistered, name,
                                              Gtk::Orientation::VERTICAL);
    b->add_css_class("jot-about-card");
    return b;
}

// One row in a card: an optional icon, a bold title, a quieter line under it.
// A hairline separates it from the row above (not before the first).
void row(widgets::Box& into, const std::string& name, const char* icon,
         const std::string& title, const std::string& line) {
    if (into.get_first_child()) {
        auto* sep = Gtk::make_managed<widgets::Box>(
            widgets::unregistered, name + ".sep", Gtk::Orientation::HORIZONTAL);
        sep->add_css_class("jot-about-row-sep");
        if (icon) sep->set_margin_start(34);
        into.append(*sep);
    }
    auto* r = Gtk::make_managed<widgets::Box>(widgets::unregistered, name,
                                              Gtk::Orientation::HORIZONTAL);
    r->set_spacing(14);
    r->add_css_class("jot-about-row");
    if (icon) {
        auto* img = Gtk::make_managed<widgets::Image>(widgets::unregistered,
                                                      name + ".icon");
        img->set_from_icon_name(icon);
        img->set_pixel_size(20);
        img->set_valign(Gtk::Align::START);
        img->set_margin_top(1);
        img->add_css_class("jot-about-row-icon");
        r->append(*img);
    }
    auto* words = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, name + ".words", Gtk::Orientation::VERTICAL);
    words->set_spacing(2);
    words->set_hexpand(true);
    auto* t = text(name + ".title", Glib::Markup::escape_text(title));
    t->add_css_class("jot-about-row-title");
    words->append(*t);
    auto* l = text(name + ".line", line);
    l->add_css_class("dim-label");
    words->append(*l);
    r->append(*words);
    into.append(*r);
}

widgets::ScrolledWindow* page_scroller(const std::string& name, Gtk::Widget& body) {
    auto* sc = Gtk::make_managed<widgets::ScrolledWindow>(widgets::unregistered, name);
    sc->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    sc->set_vexpand(true);
    sc->set_child(body);
    return sc;
}

widgets::Box* page_box(const std::string& name) {
    auto* b = Gtk::make_managed<widgets::Box>(widgets::unregistered, name,
                                              Gtk::Orientation::VERTICAL);
    b->set_spacing(4);
    b->set_margin_top(18);
    b->set_margin_bottom(22);
    b->set_margin_start(26);
    b->set_margin_end(26);
    return b;
}

// ── About: the pixie, the version, what jot is FOR and what it does ─────────
Gtk::Widget* build_about_page() {
    auto* p = page_box("shell.about.what");

    auto* logo = Gtk::make_managed<widgets::Image>(widgets::unregistered, "shell.about.logo");
    logo->set_from_icon_name("jot-app-logo-symbolic");
    logo->set_pixel_size(96);
    logo->set_halign(Gtk::Align::CENTER);
    p->append(*logo);

    auto* name = Gtk::make_managed<widgets::Label>(widgets::unregistered, "shell.about.name");
    name->set_markup("<span size='xx-large' weight='bold'>jot</span>");
    name->set_margin_top(6);
    p->append(*name);

    auto* ver = Gtk::make_managed<widgets::Label>(
        widgets::unregistered, "shell.about.version", std::string("Version ") + JOT_VERSION);
    ver->add_css_class("jot-about-version");
    p->append(*ver);

    auto* tag = Gtk::make_managed<widgets::Label>(
        widgets::unregistered, "shell.about.tagline", "A scribble pad that files itself.");
    tag->add_css_class("jot-about-tagline");
    tag->set_margin_top(6);
    p->append(*tag);

    auto* what = text("shell.about.what.text",
        "Notes and todos in one markdown tree. Write first and file later: "
        "any note can hold others, a todo is a note with a tick, and a link "
        "follows its note through every rename and move.\n\n"
        "jot gathers your work into the views that getting things done "
        "relies on, then goes a step further: a piece of work can say what "
        "<i>finished</i> means, and jot keeps track of it for you.", 50);
    what->set_justify(Gtk::Justification::CENTER);
    what->set_xalign(0.5f);
    what->set_halign(Gtk::Align::CENTER);
    what->set_margin_top(14);
    what->set_margin_bottom(4);
    p->append(*what);

    p->append(*head("shell.about.what.head", "WHAT IT DOES"));
    auto* c = card("shell.about.what.card");
    row(*c, "shell.about.what.notes", "jot-tab-notes-symbolic",
        "Notes in markdown",
        "Source, Live Preview and Reading views, a format bar, and pictures "
        "and files dropped straight into the text.");
    row(*c, "shell.about.what.capture", "jot-tab-inbox-symbolic",
        "Capture from anywhere",
        "The line in the header, a global hotkey, or <tt>jot --capture</tt> "
        "from a terminal. The Inbox sorts it out later.");
    row(*c, "shell.about.what.today", "jot-tab-today-symbolic",
        "Today and the days ahead",
        "Today, Available, Flagged, Forecast and the Logbook, with due "
        "notices and the GNOME calendar kept up to date.");
    row(*c, "shell.about.what.tags", "jot-tab-tags-symbolic",
        "Tags as contexts",
        "A #tag gathers work from anywhere in the tree; an #at/ place turns "
        "it into an errand run.");
    row(*c, "shell.about.what.projects", "jot-tab-projects-symbolic",
        "Projects that get reviewed",
        "Active, on hold or done, reviewed on a schedule, and routines that "
        "keep their own record.");
    row(*c, "shell.about.what.donewhen", "jot-state-done-symbolic",
        "Work that knows when it is done",
        "Packets gather their documents, deadlines work back from the due "
        "date, and feeders keep a goal on pace.");
    row(*c, "shell.about.what.undo", "edit-undo-symbolic",
        "Nothing is final",
        "Every change but typing is one Ctrl+Z. Ctrl+H shows every key.");
    p->append(*c);

    return page_scroller("shell.about.what.scroll", *p);
}

// ── Credits: the same three lists THIRD_PARTY.md keeps, plus the people ─────
Gtk::Widget* build_credits_page() {
    auto* p = page_box("shell.about.credits");

    p->append(*head("shell.about.credits.made.head", "MADE BY"));
    auto* made = card("shell.about.credits.made");
    row(*made, "shell.about.credits.author", nullptr, "Scott Combs",
        "Design and direction: what jot is for, and how it should feel.");
    row(*made, "shell.about.credits.logo", nullptr, "Jot, the pixie",
        "The logo, drawn by Scott Combs in Curvz.");
    row(*made, "shell.about.credits.claude", nullptr, "Claude, by Anthropic",
        "Wrote the code alongside Scott, one session at a time.");
    p->append(*made);

    p->append(*head("shell.about.credits.grew.head", "GREW FROM"));
    auto* grew = card("shell.about.credits.grew");
    row(*grew, "shell.about.credits.notr", nullptr, "Notr",
        "Scott's earlier notes tree, from its wxWidgets days. jot's tree of "
        "notes started there.");
    row(*grew, "shell.about.credits.cairn", nullptr, "Cairn",
        "The starter spine: named widgets, logging, the shortcut registry.");
    row(*grew, "shell.about.credits.family", nullptr, "Optik, Folio and Curvz",
        "Scott's other gtkmm apps, which lent jot pieces along the way.");
    p->append(*grew);

    p->append(*head("shell.about.credits.ideas.head", "IDEAS FROM"));
    auto* ideas = card("shell.about.credits.ideas");
    row(*ideas, "shell.about.credits.gtd", nullptr, "Getting Things Done",
        "David Allen's method: capture everything, then decide what it means.");
    row(*ideas, "shell.about.credits.omnifocus", nullptr, "OmniFocus",
        "The bar for projects, contexts, review and forecast.");
    row(*ideas, "shell.about.credits.obsidian", nullptr, "Obsidian",
        "Markdown files, inline tags and a live preview.");
    p->append(*ideas);

    p->append(*head("shell.about.credits.built.head", "BUILT ON"));
    auto* built = card("shell.about.credits.built");
    row(*built, "shell.about.credits.gtk", nullptr, "GTK 4 and gtkmm",
        "The toolkit, and the C++ binding jot is written against. LGPL.");
    row(*built, "shell.about.credits.spdlog", nullptr, "spdlog and {fmt}",
        "Logging, by Gabi Melman and Victor Zverovich. MIT.");
    row(*built, "shell.about.credits.json", nullptr, "JSON for Modern C++",
        "Reads and writes jot.json, by Niels Lohmann. MIT.");
#ifdef JOT_HAS_ECAL
    row(*built, "shell.about.credits.eds", nullptr, "Evolution Data Server",
        "Puts dated todos in the GNOME calendar. LGPL.");
#endif
    p->append(*built);

    // What this copy is running on -- the versions a bug report wants.
    auto* runs = Gtk::make_managed<widgets::Label>(widgets::unregistered,
        "shell.about.credits.runtime",
        "jot " JOT_VERSION " · GTK " + std::to_string(gtk_get_major_version()) + "." +
            std::to_string(gtk_get_minor_version()) + "." +
            std::to_string(gtk_get_micro_version()) + " · gtkmm " +
            std::to_string(GTKMM_MAJOR_VERSION) + "." + std::to_string(GTKMM_MINOR_VERSION) +
            "." + std::to_string(GTKMM_MICRO_VERSION));
    runs->add_css_class("jot-about-quiet");
    runs->set_selectable(true);
    runs->set_margin_top(16);
    p->append(*runs);

    return page_scroller("shell.about.credits.scroll", *p);
}

// ── Licence ─────────────────────────────────────────────────────────────────
Gtk::Widget* build_licence_page() {
    auto* p = page_box("shell.about.licence");

    auto* copy = Gtk::make_managed<widgets::Label>(widgets::unregistered,
        "shell.about.licence.copyright");
    copy->set_markup("<b>Copyright © 2026 Scott Combs</b>");
    p->append(*copy);
    auto* kind = Gtk::make_managed<widgets::Label>(widgets::unregistered,
        "shell.about.licence.kind", "jot is free software under the MIT License.");
    kind->add_css_class("dim-label");
    kind->set_margin_bottom(10);
    p->append(*kind);

    auto* c = card("shell.about.licence.card");
    auto* body = text("shell.about.licence.text",
        Glib::Markup::escape_text(
            "Permission is hereby granted, free of charge, to any person obtaining "
            "a copy of this software and associated documentation files (the "
            "\"Software\"), to deal in the Software without restriction, including "
            "without limitation the rights to use, copy, modify, merge, publish, "
            "distribute, sublicense, and/or sell copies of the Software, and to "
            "permit persons to whom the Software is furnished to do so, subject to "
            "the following conditions:\n\n"
            "The above copyright notice and this permission notice shall be "
            "included in all copies or substantial portions of the Software.\n\n"
            "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, "
            "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
            "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND "
            "NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS "
            "BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN "
            "ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN "
            "CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
            "SOFTWARE."), 52);
    body->add_css_class("jot-about-licence");
    body->set_margin_top(10);
    body->set_margin_bottom(10);
    c->append(*body);
    p->append(*c);

    auto* site = Gtk::make_managed<widgets::LinkButton>(
        widgets::unregistered, "shell.about.licence.site", kRepoUrl, "jot on GitHub");
    site->set_halign(Gtk::Align::CENTER);
    site->set_margin_top(12);
    p->append(*site);

    auto* also = Gtk::make_managed<widgets::Label>(widgets::unregistered,
        "shell.about.licence.also",
        "The libraries under Credits keep their own licences; "
        "THIRD_PARTY.md lists them.");
    also->add_css_class("jot-about-quiet");
    also->set_wrap(true);
    also->set_justify(Gtk::Justification::CENTER);
    also->set_max_width_chars(46);
    also->set_margin_top(4);
    p->append(*also);

    return page_scroller("shell.about.licence.scroll", *p);
}

}  // namespace

AboutWindow::AboutWindow(Gtk::Window& parent) {
    set_name("shell.about");
    registry::add("shell.about", this);
    install_about_css();

    set_title("About jot");
    set_modal(true);
    set_resizable(false);
    set_default_size(500, 660);
    set_transient_for(parent);
    set_hide_on_close(true);   // singleton: X hides, never destructs mid-session

    auto* stack = Gtk::make_managed<widgets::Stack>(widgets::unregistered, "shell.about.stack");
    stack->set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    stack->add(*build_about_page(), "about", "About");
    stack->add(*build_credits_page(), "credits", "Credits");
    stack->add(*build_licence_page(), "licence", "Licence");

    // The switcher sits in the title bar, where a Mac puts a window's tabs.
    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(true);
    auto* sw = Gtk::make_managed<widgets::StackSwitcher>(widgets::unregistered,
                                                         "shell.about.switcher");
    sw->set_stack(*stack);
    hb->set_title_widget(*sw);
    set_titlebar(*hb);
    set_child(*stack);

    // Esc closes, as every other jot dialog does.
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint key, guint, Gdk::ModifierType) {
            if (key != GDK_KEY_Escape) return false;
            close();
            return true;
        }, false);
    add_controller(keys);

    // Each time it is shown it opens on About, not wherever it was left.
    // Focus goes to the switcher, so no label opens with its text selected.
    signal_show().connect([stack, sw]() {
        stack->set_visible_child("about");
        if (auto* first = sw->get_first_child()) first->grab_focus();
    });

    if (auto lg = log::get(log::Area::Shell))
        lg->info("about: built, version {}", JOT_VERSION);
}

AboutWindow::~AboutWindow() {
    registry::remove(this);
}

}  // namespace jot
