#include "HelpWindow.hpp"
#include "CheatSheetPage.hpp"
#include "core/Help.hpp"
#include "Log.hpp"
#include "Registry.hpp"

#include <gtkmm/cssprovider.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/listboxrow.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

// HelpWindow.cpp -- draws core::help_topics() and the cheat sheet. See the header.

namespace jot {

namespace {

void log_info(const std::string& s) {
    if (auto lg = log::get(log::Area::Shell)) lg->info("help: {}", s);
}

void install_help_css() {
    static bool done = false;
    if (done) return;
    auto display = Gdk::Display::get_default();
    if (!display) return;
    auto css = Gtk::CssProvider::create();
    // The accent-coloured parts are Appearance's (they follow the accent live);
    // these are the shapes.
    css->load_from_data(
        ".jot-help-side { background-color: alpha(currentColor, 0.045); }"
        ".jot-help-side list { background: transparent; }"
        ".jot-help-side row { padding: 5px 8px; margin: 0 6px; }"
        ".jot-help-side row:selected { color: inherit; }"
        ".jot-help-group { font-size: 0.78em; font-weight: 700; letter-spacing: 0.06em;"
        "  opacity: 0.55; padding: 12px 10px 3px 10px; }"
        ".jot-help-kicker { font-size: 0.8em; font-weight: 700; letter-spacing: 0.06em;"
        "  opacity: 0.55; }"
        ".jot-help-lead { font-size: 1.12em; opacity: 0.8; }"
        ".jot-help-head { font-size: 0.78em; font-weight: 700; letter-spacing: 0.06em;"
        "  opacity: 0.55; margin-top: 18px; margin-bottom: 2px; }"
        ".jot-help-bullet { opacity: 0.55; }"
        ".jot-help-keys { font-size: 0.86em; opacity: 0.6; }"
        ".jot-help-see { padding: 4px 10px; border-radius: 8px; }"
        ".jot-help-foot { margin-top: 26px; }");
    gtk_style_context_add_provider_for_display(display->gobj(), GTK_STYLE_PROVIDER(css->gobj()),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    done = true;
}

widgets::Label* para(const std::string& name, const std::string& markup) {
    auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered, name);
    l->set_markup(markup);
    l->set_wrap(true);
    l->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
    l->set_xalign(0.0f);
    l->set_max_width_chars(66);
    l->set_selectable(true);   // a command is something you copy
    return l;
}

widgets::Label* small_head(const std::string& name, const std::string& words) {
    auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered, name, words);
    l->set_xalign(0.0f);
    l->add_css_class("jot-help-head");
    return l;
}

}  // namespace

HelpWindow::HelpWindow()
    : m_stack("help.stack"),
      m_switcher("help.switcher"),
      m_find("help.find"),
      m_list("help.list"),
      m_page_scroll("help.page.scroll"),
      m_page("help.page", Gtk::Orientation::VERTICAL, 6),
      m_no_match("help.nomatch") {
    set_name("shell.help");
    registry::add("shell.help", this);
    install_help_css();

    set_title("jot Help");
    set_modal(false);
    set_resizable(true);
    set_default_size(880, 700);
    set_hide_on_close(true);   // built once by the Shell, re-presented

    build_guide();
    m_cheat = Gtk::make_managed<CheatSheetPage>();
    m_stack.add(*m_cheat, "keys", "Cheat Sheet");
    m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);

    // The switcher sits in the title bar, where a Mac puts a window's tabs.
    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(true);
    m_switcher.set_stack(m_stack);
    hb->set_title_widget(m_switcher);
    set_titlebar(*hb);
    set_child(m_stack);

    // Switched by the reader: the page's search takes the keys.
    m_stack.property_visible_child_name().signal_changed().connect([this]() {
        if (!get_visible()) return;
        if (m_stack.get_visible_child_name() == "keys") m_cheat->focus_search();
        else m_find.grab_focus();
    });

    // Escape: a search typed is emptied first; then the window goes.
    auto esc = Gtk::EventControllerKey::create();
    esc->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    esc->signal_key_pressed().connect(
        [this](guint key, guint, Gdk::ModifierType mods) -> bool {
            if (key == GDK_KEY_Escape) return on_escape();
            // The Help keys work here too: this is not the app window, so the
            // app's accelerators do not reach it. Ctrl+H the Cheat Sheet; F1 /
            // Ctrl+Shift+H the Guide; Ctrl+F the page's search.
            const auto m = mods & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK |
                                   Gdk::ModifierType::ALT_MASK);
            const bool ctrl = m == Gdk::ModifierType::CONTROL_MASK;
            const bool ctrl_shift =
                m == (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK);
            if (ctrl && (key == GDK_KEY_h || key == GDK_KEY_H)) {
                log_info("key: Cheat Sheet");
                m_stack.set_visible_child("keys");
                m_cheat->focus_search();
                return true;
            }
            if (key == GDK_KEY_F1 || (ctrl_shift && (key == GDK_KEY_h || key == GDK_KEY_H))) {
                log_info("key: Guide");
                m_stack.set_visible_child("guide");
                m_find.grab_focus();
                return true;
            }
            if (ctrl && (key == GDK_KEY_f || key == GDK_KEY_F)) {
                if (m_stack.get_visible_child_name() == "keys") m_cheat->focus_search();
                else m_find.grab_focus();
                return true;
            }
            return false;
        },
        false);
    add_controller(esc);

    log_info("built, " + std::to_string(core::help_topics().size()) + " pages");
}

HelpWindow::~HelpWindow() { registry::remove(this); }

bool HelpWindow::on_escape() {
    if (m_stack.get_visible_child_name() == "keys") {
        if (m_cheat->clear_search()) return true;
    } else if (!m_find.get_text().empty()) {
        m_find.set_text("");
        return true;
    }
    set_visible(false);
    return true;
}

int HelpWindow::visible_lines() const { return m_cheat ? m_cheat->visible_lines() : 0; }

// ── The guide: a source list of pages, and the page ─────────────────────────
void HelpWindow::build_guide() {
    auto* guide = Gtk::make_managed<widgets::Box>(widgets::unregistered, "help.guide",
                                                  Gtk::Orientation::HORIZONTAL, 0);

    auto* side = Gtk::make_managed<widgets::Box>(widgets::unregistered, "help.side",
                                                 Gtk::Orientation::VERTICAL, 4);
    side->add_css_class("jot-help-side");
    side->set_size_request(232, -1);
    m_find.set_placeholder_text("Find a page");
    m_find.set_margin(10);
    m_find.set_margin_bottom(2);
    m_find.signal_search_changed().connect([this]() { filter_sidebar(); });
    m_find.signal_activate().connect([this]() {
        // Enter: the first page that matches.
        for (const auto& r : m_rows)
            if (!r.topic.empty() && r.row->get_visible()) {
                show_topic(r.topic);
                return;
            }
    });
    side->append(m_find);

    m_list.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_list.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (m_selecting || !row) return;
        for (const auto& r : m_rows)
            if (r.row == row && !r.topic.empty()) {
                show_topic(r.topic);
                return;
            }
    });
    auto* side_scroll = Gtk::make_managed<widgets::ScrolledWindow>(widgets::unregistered,
                                                                   "help.side.scroll");
    side_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    side_scroll->set_vexpand(true);
    side_scroll->set_child(m_list);
    side->append(*side_scroll);

    m_no_match.set_text("No page has all of those words.");
    m_no_match.add_css_class("dim-label");
    m_no_match.set_wrap(true);
    m_no_match.set_margin(12);
    m_no_match.set_visible(false);
    side->append(m_no_match);

    guide->append(*side);
    guide->append(*Gtk::make_managed<widgets::Separator>(widgets::unregistered, "help.rule",
                                                         Gtk::Orientation::VERTICAL));

    m_page.set_margin_top(26);
    m_page.set_margin_bottom(26);
    m_page.set_margin_start(36);
    m_page.set_margin_end(36);
    m_page_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_page_scroll.set_hexpand(true);
    m_page_scroll.set_vexpand(true);
    m_page_scroll.set_child(m_page);
    guide->append(m_page_scroll);

    m_stack.add(*guide, "guide", "Guide");
    fill_sidebar();
    show_topic(m_topic);
}

void HelpWindow::fill_sidebar() {
    const auto& groups = core::help_groups();
    const auto& topics = core::help_topics();
    for (std::size_t g = 0; g < groups.size(); ++g) {
        auto* hrow = Gtk::make_managed<Gtk::ListBoxRow>();
        hrow->set_name("help.list.group");
        hrow->set_selectable(false);
        hrow->set_activatable(false);
        hrow->set_can_focus(false);
        std::string up = groups[g];
        for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        auto* hl = Gtk::make_managed<Gtk::Label>(up);
        hl->set_xalign(0.0f);
        hl->add_css_class("jot-help-group");
        hrow->set_child(*hl);
        m_list.append(*hrow);
        m_rows.push_back({hrow, "", g});

        for (const auto& t : topics) {
            if (t.group != groups[g]) continue;
            auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
            row->set_name("help.list." + t.id);
            auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
            auto* img = Gtk::make_managed<Gtk::Image>();
            img->set_from_icon_name(t.icon);
            img->add_css_class("jot-help-icon");
            box->append(*img);
            auto* l = Gtk::make_managed<Gtk::Label>(t.title);
            l->set_xalign(0.0f);
            l->set_ellipsize(Pango::EllipsizeMode::END);
            box->append(*l);
            row->set_child(*box);
            row->set_tooltip_text(t.lead);
            m_list.append(*row);
            m_rows.push_back({row, t.id, g});
        }
    }
}

void HelpWindow::filter_sidebar() {
    const std::string q = m_find.get_text();
    std::vector<int> per(core::help_groups().size(), 0);
    int shown = 0;
    for (auto& r : m_rows) {
        if (r.topic.empty()) continue;
        const auto* t = core::help_topic(r.topic);
        const bool on = t && core::help_matches(*t, q);
        r.row->set_visible(on);
        if (on) {
            ++per[r.group];
            ++shown;
        }
    }
    for (auto& r : m_rows)
        if (r.topic.empty()) r.row->set_visible(per[r.group] > 0);
    m_no_match.set_visible(shown == 0);
    if (!q.empty()) log_info("find '" + q + "' -> " + std::to_string(shown) + " page(s)");
}

void HelpWindow::show_topic(const std::string& id) {
    if (!core::help_topic(id)) return;
    m_topic = id;
    m_selecting = true;
    Gtk::ListBoxRow* sel = nullptr;
    for (const auto& r : m_rows)
        if (r.topic == id) {
            m_list.select_row(*r.row);
            sel = r.row;
        }
    m_selecting = false;
    // The button pressed (Next, a See also) is about to go with the old page,
    // and GTK would hand focus to the next focusable thing -- a selectable
    // label, which then shows itself all selected. Move it first, to the page's
    // row in the sidebar: the arrow keys walk the pages from there.
    if (auto* f = get_focus(); f && sel && (f == &m_page || f->is_ancestor(m_page)))
        sel->grab_focus();
    fill_page(id);
    log_info("page '" + id + "'");
}

void HelpWindow::fill_page(const std::string& id) {
    const core::HelpTopic* t = core::help_topic(id);
    if (!t) return;
    while (auto* c = m_page.get_first_child()) m_page.remove(*c);
    const std::string base = "help.page." + t->id;

    // The head: the part's icon in the accent, the group small, the title large.
    auto* head = Gtk::make_managed<widgets::Box>(widgets::unregistered, base + ".head",
                                                 Gtk::Orientation::HORIZONTAL, 16);
    auto* img = Gtk::make_managed<widgets::Image>(widgets::unregistered, base + ".icon");
    img->set_from_icon_name(t->icon);
    img->set_pixel_size(44);
    img->add_css_class("jot-help-icon");
    img->set_valign(Gtk::Align::CENTER);
    head->append(*img);
    auto* words = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    words->set_valign(Gtk::Align::CENTER);
    std::string kicker = t->group;
    for (auto& c : kicker) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto* k = Gtk::make_managed<Gtk::Label>(kicker);
    k->set_xalign(0.0f);
    k->add_css_class("jot-help-kicker");
    words->append(*k);
    auto* title = Gtk::make_managed<widgets::Label>(widgets::unregistered, base + ".title");
    title->set_markup("<span size='xx-large' weight='bold'>" + core::help_markup(t->title) +
                      "</span>");
    title->set_xalign(0.0f);
    title->set_wrap(true);
    words->append(*title);
    head->append(*words);
    m_page.append(*head);

    auto* lead = para(base + ".lead", core::help_markup(t->lead));
    lead->set_selectable(false);   // a headline, not a command to copy
    lead->add_css_class("jot-help-lead");
    lead->set_margin_top(10);
    lead->set_margin_bottom(6);
    m_page.append(*lead);

    // The body: paragraphs, and "- " lines as bullets with a hanging indent.
    int n = 0;
    for (const auto& p : t->body) {
        const std::string name = base + ".p" + std::to_string(n++);
        if (p.rfind("- ", 0) == 0) {
            auto* b = Gtk::make_managed<widgets::Box>(widgets::unregistered, name,
                                                      Gtk::Orientation::HORIZONTAL, 10);
            b->set_margin_start(6);
            auto* dot = Gtk::make_managed<Gtk::Label>("•");
            dot->add_css_class("jot-help-bullet");
            dot->set_valign(Gtk::Align::START);
            b->append(*dot);
            auto* l = para(name + ".text", core::help_markup(p.substr(2)));
            l->set_hexpand(true);
            b->append(*l);
            m_page.append(*b);
        } else {
            auto* l = para(name, core::help_markup(p));
            l->set_margin_top(6);
            m_page.append(*l);
        }
    }

    // Try it: the verbs this page is about, each with its key.
    if (!t->tries.empty()) {
        m_page.append(*small_head(base + ".try.head", "TRY IT"));
        auto* tries = Gtk::make_managed<widgets::Box>(widgets::unregistered, base + ".tries",
                                                      Gtk::Orientation::HORIZONTAL, 8);
        for (const auto& tr : t->tries) {
            auto* b = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                         base + ".try." + tr.action);
            b->add_css_class("flat");
            b->add_css_class("jot-help-try");
            auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            box->append(*Gtk::make_managed<Gtk::Label>(tr.label));
            const std::string keys = core::help_try_keys(tr);
            if (!keys.empty()) {
                auto* kl = Gtk::make_managed<Gtk::Label>(keys);
                kl->add_css_class("jot-help-keys");
                box->append(*kl);
            }
            b->set_child(*box);
            const std::string action = tr.action;
            b->signal_clicked().connect([this, action]() {
                log_info("try " + action);
                m_sig_try.emit(action);
            });
            tries->append(*b);
        }
        m_page.append(*tries);
    }

    // Every key for this: the cheat sheet, filtered to the page.
    if (!t->keys.empty()) {
        auto* kb = Gtk::make_managed<widgets::Button>(widgets::unregistered, base + ".keys");
        kb->add_css_class("flat");
        auto* kl = Gtk::make_managed<Gtk::Label>("Every key for this, in the Cheat Sheet ›");
        kl->add_css_class("jot-help-link");
        kb->set_child(*kl);
        kb->set_halign(Gtk::Align::START);
        kb->set_margin_top(10);
        const std::string q = t->keys;
        kb->signal_clicked().connect([this, q]() {
            m_cheat->reset(q);
            m_stack.set_visible_child("keys");
            m_cheat->focus_search();
            log_info("keys '" + q + "' -> " + std::to_string(m_cheat->visible_lines()) + " line(s)");
        });
        m_page.append(*kb);
    }

    // See also.
    if (!t->see.empty()) {
        m_page.append(*small_head(base + ".see.head", "SEE ALSO"));
        auto* see = Gtk::make_managed<widgets::Box>(widgets::unregistered, base + ".see",
                                                    Gtk::Orientation::HORIZONTAL, 4);
        for (const auto& sid : t->see) {
            const core::HelpTopic* o = core::help_topic(sid);
            if (!o) continue;
            auto* b = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                         base + ".see." + sid);
            b->add_css_class("flat");
            b->add_css_class("jot-help-see");
            auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            auto* im = Gtk::make_managed<Gtk::Image>();
            im->set_from_icon_name(o->icon);
            im->add_css_class("jot-help-icon");
            box->append(*im);
            box->append(*Gtk::make_managed<Gtk::Label>(o->title));
            b->set_child(*box);
            b->set_tooltip_text(o->lead);
            b->signal_clicked().connect([this, sid]() { show_topic(sid); });
            see->append(*b);
        }
        m_page.append(*see);
    }

    // Previous / Next: the guide reads as a tour, start to finish.
    const auto& all = core::help_topics();
    const int i = core::help_index(id);
    auto* rule = Gtk::make_managed<widgets::Separator>(widgets::unregistered, base + ".foot.rule",
                                                       Gtk::Orientation::HORIZONTAL);
    rule->add_css_class("jot-help-foot");
    m_page.append(*rule);
    auto* nav = Gtk::make_managed<widgets::Box>(widgets::unregistered, base + ".nav",
                                                Gtk::Orientation::HORIZONTAL, 8);
    nav->set_margin_top(8);
    auto add_nav = [&](int j, bool next) {
        auto* b = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                     next ? base + ".next" : base + ".prev");
        b->add_css_class("flat");
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        auto* small = Gtk::make_managed<Gtk::Label>(next ? "Next" : "Previous");
        small->add_css_class("jot-help-keys");
        auto* name = Gtk::make_managed<Gtk::Label>(next ? all[j].title + "  ›"
                                                        : "‹  " + all[j].title);
        name->add_css_class("jot-help-link");
        small->set_xalign(next ? 1.0f : 0.0f);
        name->set_xalign(next ? 1.0f : 0.0f);
        box->append(*small);
        box->append(*name);
        b->set_child(*box);
        const std::string to = all[j].id;
        b->signal_clicked().connect([this, to]() { show_topic(to); });
        if (next) {
            b->set_hexpand(true);
            b->set_halign(Gtk::Align::END);
        }
        nav->append(*b);
    };
    if (i > 0) add_nav(i - 1, false);
    if (i >= 0 && i + 1 < static_cast<int>(all.size())) add_nav(i + 1, true);
    m_page.append(*nav);

    m_page_scroll.get_vadjustment()->set_value(0.0);
}

// ── Showing ─────────────────────────────────────────────────────────────────
void HelpWindow::show_guide(Gtk::Window& parent, const std::string& topic) {
    set_transient_for(parent);
    m_stack.set_visible_child("guide");
    if (!topic.empty() && topic != m_topic) show_topic(topic);
    present();
    m_find.grab_focus();
    log_info("guide open on '" + m_topic + "'");
}

void HelpWindow::show_keys(Gtk::Window& parent, const std::string& query) {
    set_transient_for(parent);
    m_cheat->reset(query);
    m_stack.set_visible_child("keys");
    present();
    m_cheat->focus_search();
    log_info("cheat sheet open, " + std::to_string(m_cheat->visible_lines()) + " lines");
}

}  // namespace jot
