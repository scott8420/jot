#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"

#include <gtkmm/cssprovider.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/flowboxchild.h>
#include <gtkmm/headerbar.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

// ShortcutsDialog.cpp -- draws core::key_rows(). See the header.

namespace jot {

namespace {

void log_info(const std::string& s) {
    if (auto lg = log::get(log::Area::Shell)) lg->info("shortcuts: {}", s);
}

void install_css() {
    static bool done = false;
    if (done) return;
    auto display = Gdk::Display::get_default();
    if (!display) return;
    auto css = Gtk::CssProvider::create();
    // The CAP itself is drawn by Appearance's sheet, beside its lit form: GTK
    // settles each property provider by provider (a later provider wins over an
    // earlier one whatever the selectors), so the two must share a provider.
    css->load_from_data(
        ".jot-keys-card { background-color: alpha(currentColor, 0.045);"
        "  border-radius: 12px; padding: 6px 14px 8px 14px; }"
        ".jot-keys-head { font-size: 0.78em; font-weight: 700; letter-spacing: 0.06em;"
        "  opacity: 0.55; margin: 6px 2px 4px 2px; }"
        ".jot-keys-row { padding: 7px 0; }"
        ".jot-keys-sep { background-color: alpha(currentColor, 0.08); min-height: 1px; }"
        ".jot-keys-where { font-size: 0.84em; opacity: 0.55; }"
        ".jot-keys-or { font-size: 0.84em; opacity: 0.5; margin: 0 2px; }"
        ".jot-keys-foot { margin-top: 2px; }");
    gtk_style_context_add_provider_for_display(display->gobj(), GTK_STYLE_PROVIDER(css->gobj()),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    done = true;
}

}  // namespace

ShortcutsDialog::ShortcutsDialog()
    : m_search("shortcuts.search"),
      m_scroll("shortcuts.scroll"),
      m_cards("shortcuts.cards"),
      m_none("shortcuts.none") {
    set_name("shell.shortcuts");
    registry::add("shell.shortcuts", this);
    install_css();

    set_title("Keyboard Shortcuts");
    set_modal(false);
    set_resizable(true);
    set_default_size(1040, 720);
    set_hide_on_close(true);   // built once by the Shell, re-presented

    // The search in the title bar, as GNOME's dialog has it.
    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(true);
    m_search.set_placeholder_text("Search shortcuts — words or keys: “flag”, “ctrl+m”");
    m_search.set_size_request(380, -1);
    m_search.signal_search_changed().connect([this]() { filter(); });
    hb->set_title_widget(m_search);
    set_titlebar(*hb);
    // Typing anywhere in the window starts a search.
    m_search.set_key_capture_widget(*this);

    build();
    filter();

    // Escape: a search typed is emptied first; then the window goes. Ctrl+F
    // takes the search.
    auto keys = Gtk::EventControllerKey::create();
    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    keys->signal_key_pressed().connect(
        [this](guint key, guint, Gdk::ModifierType mods) -> bool {
            if (key == GDK_KEY_Escape) {
                if (!m_search.get_text().empty()) m_search.set_text("");
                else set_visible(false);
                return true;
            }
            const auto m = mods & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK |
                                   Gdk::ModifierType::ALT_MASK);
            if (m == Gdk::ModifierType::CONTROL_MASK && (key == GDK_KEY_f || key == GDK_KEY_F)) {
                m_search.grab_focus();
                return true;
            }
            return false;
        },
        false);
    add_controller(keys);

    log_info("built, " + std::to_string(m_rows.size()) + " rows in " +
             std::to_string(m_card_widgets.size()) + " sections");
}

ShortcutsDialog::~ShortcutsDialog() { registry::remove(this); }

void ShortcutsDialog::show(Gtk::Window& parent) {
    set_transient_for(parent);
    m_search.set_text("");   // filter() runs off the change
    m_scroll.get_vadjustment()->set_value(0.0);
    present();
    m_search.grab_focus();
    log_info("open, " + std::to_string(m_visible) + " rows");
}

void ShortcutsDialog::build() {
    m_rows_data = core::key_rows();

    auto* page = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shortcuts.page",
                                                 Gtk::Orientation::VERTICAL, 10);
    page->set_margin(18);

    m_cards.set_selection_mode(Gtk::SelectionMode::NONE);
    m_cards.set_homogeneous(false);
    m_cards.set_min_children_per_line(1);
    m_cards.set_max_children_per_line(3);
    m_cards.set_column_spacing(16);
    m_cards.set_row_spacing(16);
    m_cards.set_valign(Gtk::Align::START);
    m_cards.set_activate_on_single_click(false);

    const auto& sections = core::key_sections();
    for (std::size_t c = 0; c < sections.size(); ++c) {
        bool any = false;
        for (const auto& r : m_rows_data)
            if (r.section == sections[c]) any = true;
        if (!any) continue;

        auto* col = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                    "shortcuts.section." + sections[c],
                                                    Gtk::Orientation::VERTICAL, 0);
        col->set_size_request(300, -1);
        std::string up = sections[c];
        for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        auto* head = Gtk::make_managed<Gtk::Label>(up);
        head->set_xalign(0.0f);
        head->add_css_class("jot-keys-head");
        col->append(*head);
        auto* card = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                     "shortcuts.card." + sections[c],
                                                     Gtk::Orientation::VERTICAL, 0);
        card->add_css_class("jot-keys-card");
        col->append(*card);

        const std::size_t card_index = m_card_widgets.size();
        bool first = true;
        for (std::size_t i = 0; i < m_rows_data.size(); ++i) {
            const auto& r = m_rows_data[i];
            if (r.section != sections[c]) continue;
            Row row;
            row.row = i;
            row.card = card_index;
            if (!first) {
                auto* sep = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
                sep->add_css_class("jot-keys-sep");
                card->append(*sep);
                row.sep = sep;
            }
            first = false;

            auto* line = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shortcuts.row",
                                                         Gtk::Orientation::HORIZONTAL, 12);
            line->add_css_class("jot-keys-row");
            auto* words = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 1);
            words->set_hexpand(true);
            words->set_valign(Gtk::Align::CENTER);
            auto* d = Gtk::make_managed<Gtk::Label>(r.description);
            d->set_xalign(0.0f);
            d->set_wrap(true);
            d->set_max_width_chars(30);
            words->append(*d);
            if (!r.where.empty()) {
                auto* w = Gtk::make_managed<Gtk::Label>(r.where);
                w->set_xalign(0.0f);
                w->add_css_class("jot-keys-where");
                words->append(*w);
            }
            line->append(*words);

            // The caps: alternatives stacked when there are two, "or" between.
            auto* keys = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
            keys->set_valign(Gtk::Align::CENTER);
            keys->set_halign(Gtk::Align::END);
            for (std::size_t a = 0; a < r.keys.size(); ++a) {
                auto* alt = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 3);
                alt->set_halign(Gtk::Align::END);
                if (a > 0) {
                    auto* orl = Gtk::make_managed<Gtk::Label>("or");
                    orl->add_css_class("jot-keys-or");
                    alt->append(*orl);
                }
                for (const auto& k : r.keys[a]) {
                    auto* cap = Gtk::make_managed<Gtk::Label>(k);
                    cap->add_css_class("jot-keycap");
                    alt->append(*cap);
                    row.caps.push_back(cap);
                    row.cap_alt.push_back(a);
                }
                keys->append(*alt);
            }
            line->append(*keys);
            if (!r.action.empty()) line->set_tooltip_text(r.action);
            card->append(*line);
            row.widget = line;
            m_rows.push_back(row);
        }
        m_cards.append(*col);
        m_card_widgets.push_back(col->get_parent());   // the FlowBoxChild
    }

    page->append(m_cards);

    m_none.set_wrap(true);
    m_none.add_css_class("dim-label");
    m_none.set_margin_top(30);
    m_none.set_visible(false);
    page->append(m_none);

    // The foot: what is not a key lives on the cheat sheet; why, in the guide.
    auto* foot = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shortcuts.foot",
                                                 Gtk::Orientation::HORIZONTAL, 6);
    foot->add_css_class("jot-keys-foot");
    foot->set_halign(Gtk::Align::CENTER);
    auto* lead = Gtk::make_managed<Gtk::Label>("Drags, clicks, markdown marks and the command line:");
    lead->add_css_class("dim-label");
    foot->append(*lead);
    auto* cheat = Gtk::make_managed<widgets::Button>(widgets::unregistered, "shortcuts.cheat");
    cheat->add_css_class("flat");
    auto* cl = Gtk::make_managed<Gtk::Label>("the Cheat Sheet ›");
    cl->add_css_class("jot-help-link");
    cheat->set_child(*cl);
    cheat->signal_clicked().connect([this]() { m_sig_cheat.emit(); });
    foot->append(*cheat);
    auto* guide = Gtk::make_managed<widgets::Button>(widgets::unregistered, "shortcuts.guide");
    guide->add_css_class("flat");
    auto* gl = Gtk::make_managed<Gtk::Label>("How jot works: the Guide ›");
    gl->add_css_class("jot-help-link");
    guide->set_child(*gl);
    guide->signal_clicked().connect([this]() { m_sig_guide.emit(); });
    foot->append(*guide);
    page->append(*foot);

    m_scroll.set_child(*page);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    set_child(m_scroll);
}

void ShortcutsDialog::filter() {
    const std::string q = m_search.get_text();
    std::vector<int> per(m_card_widgets.size(), 0);
    std::vector<bool> first_in_card(m_card_widgets.size(), true);
    m_visible = 0;
    int lit_caps = 0;
    for (auto& r : m_rows) {
        const int hit = core::key_row_match(m_rows_data[r.row], q);
        const bool on = hit > 0;
        r.widget->set_visible(on);
        // The hairline goes with its row, and the first row SHOWN in a card has none.
        if (r.sep) r.sep->set_visible(on && !first_in_card[r.card]);
        if (on) {
            first_in_card[r.card] = false;
            ++per[r.card];
            ++m_visible;
        }
        // Found by its keys (and something was typed): light the alternatives
        // the keys point at -- "shift" lights Ctrl+Shift+Z, not Ctrl+Z.
        const auto lit = (hit == 2 && !q.empty())
                             ? core::key_lit_alternatives(m_rows_data[r.row], q)
                             : std::vector<bool>(m_rows_data[r.row].keys.size(), false);
        for (std::size_t i = 0; i < r.caps.size(); ++i) {
            auto* c = r.caps[i];
            if (lit[r.cap_alt[i]]) { c->add_css_class("jot-keycap-lit"); ++lit_caps; }
            else c->remove_css_class("jot-keycap-lit");
        }
    }
    for (std::size_t c = 0; c < m_card_widgets.size(); ++c)
        if (m_card_widgets[c]) m_card_widgets[c]->set_visible(per[c] > 0);
    m_none.set_text("No shortcut matches “" + q + "”. The Cheat Sheet has the drags "
                    "and clicks too.");
    m_none.set_visible(m_visible == 0);
    if (!q.empty()) log_info("'" + q + "' -> " + std::to_string(m_visible) + " row(s), " +
                             std::to_string(lit_caps) + " cap(s) lit");
}

}  // namespace jot
