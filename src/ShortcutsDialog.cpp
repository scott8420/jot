#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"

#include <gtkmm/cssprovider.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/flowboxchild.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/accelerator.h>
#include <glibmm/main.h>
#include "core/Hotkey.hpp"
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
        ".jot-keys-foot { margin-top: 2px; }"
        ".jot-keys-editpad { padding-left: 6px; padding-right: 6px; }"
        ".jot-keys-note { font-size: 0.9em; color: #c01c28; margin-bottom: 6px; }"
        ".jot-keys-ask { font-size: 0.9em; }");
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
    // s070: Edit -- the same list becomes the place to change a key.
    m_edit.set_label("Edit");
    m_edit.set_tooltip_text("Change a shortcut: click it, then press the new key");
    m_edit.signal_toggled().connect([this]() { set_editing(m_edit.get_active()); });
    hb->pack_end(m_edit);
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
            if (!m_capture.empty()) return on_capture_key(key, mods);   // s070: a row waits for a key
            if (key == GDK_KEY_Escape && (!m_pending_action.empty() || !m_note.empty())) {
                clear_edit_state();
                queue_fill();
                return true;
            }
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

void ShortcutsDialog::show(Gtk::Window& parent, bool editing) {
    set_transient_for(parent);
    if (m_editing != editing) set_editing(editing);
    m_search.set_text("");   // filter() runs off the change
    m_scroll.get_vadjustment()->set_value(0.0);
    present();
    m_search.grab_focus();
    log_info("open, " + std::to_string(m_visible) + " rows");
}

void ShortcutsDialog::build() {

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

    fill_cards();

    // s070: what Edit means, while it is on.
    m_edit_bar.set_text("Click a shortcut, then press its new key.  Esc cancels \u00b7 "
                        "Backspace leaves it with no key \u00b7 \u21ba puts back jot's own.  "
                        "Preferences \u203a Keyboard resets them all.");
    m_edit_bar.set_wrap(true);
    m_edit_bar.set_xalign(0.0f);
    m_edit_bar.add_css_class("jot-keys-editbar");
    m_edit_bar.set_visible(false);
    page->append(m_edit_bar);
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

// The cards, from core::key_rows() -- built again whenever a key changes or
// Edit turns on / off (a few dozen rows: cheap, and nothing can go stale).
void ShortcutsDialog::fill_cards() {
    while (auto* c = m_cards.get_first_child()) m_cards.remove(*c);
    m_rows.clear();
    m_card_widgets.clear();
    m_rows_data = core::key_rows(m_editing);   // editing: the key-less verbs too

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
            const bool changed = !r.action.empty() && core::key_overrides().count(r.action);
            const bool capturing = m_editing && !r.action.empty() && r.action == m_capture;
            if (capturing) {
                auto* ask = Gtk::make_managed<Gtk::Label>("Press the new key\u2026");
                ask->add_css_class("jot-keycap");
                ask->add_css_class("jot-keycap-lit");
                keys->append(*ask);
            } else if (r.keys.empty()) {
                auto* none = Gtk::make_managed<Gtk::Label>(m_editing ? "no key \u2014 click to give one"
                                                                     : "no key");
                none->add_css_class("jot-keys-where");
                keys->append(*none);
            }
            for (std::size_t a = 0; a < r.keys.size() && !capturing; ++a) {
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
                    if (changed) cap->add_css_class("jot-keycap-changed");
                    alt->append(*cap);
                    row.caps.push_back(cap);
                    row.cap_alt.push_back(a);
                }
                keys->append(*alt);
            }
            line->append(*keys);

            // s070: ↺ on a changed key -- jot's own back, this one only.
            if (changed) {
                auto* back = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                                "shortcuts.reset." + r.action);
                back->set_icon_name("edit-undo-symbolic");
                back->add_css_class("flat");
                back->set_valign(Gtk::Align::CENTER);
                std::string was;
                for (const auto& d2 : core::shortcut_defaults())
                    if (d2.action == r.action) was = d2.display_keys();
                back->set_tooltip_text("Put back jot's own key" + (was.empty() ? std::string("") : ": " + was));
                const std::string action = r.action;
                back->signal_clicked().connect([this, action]() {
                    std::vector<std::string> took;
                    const auto o = core::reset_key(core::key_overrides(), action, &took);
                    log_info("reset " + action + (took.empty() ? "" : " -- taken back from " + took[0]));
                    clear_edit_state();
                    if (!took.empty()) {
                        // Say it under the row that lost its key.
                        std::string what;
                        for (const auto& d2 : core::shortcut_defaults())
                            if (d2.action == action) what = d2.description;
                        m_note_action = took[0];
                        m_note = "Its key went back to: " + what + ". This one has no key now \u2014 click to give it one.";
                    }
                    m_sig_keys.emit(o);
                });
                line->append(*back);
            }

            // The block: the line, and under it what the last key press said.
            auto* block = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shortcuts.block",
                                                          Gtk::Orientation::VERTICAL, 4);
            block->append(*line);
            if (m_editing) line->add_css_class("jot-keys-editpad");   // every row the same inset
            if (m_editing && !r.action.empty()) {
                line->add_css_class("jot-keys-editable");
                line->set_tooltip_text("Click, then press the new key");
                auto click = Gtk::GestureClick::create();
                const std::string action = r.action;
                click->signal_released().connect([this, action](int, double, double) {
                    begin_capture(action);
                });
                line->add_controller(click);
                if (r.action == m_note_action && !m_note.empty()) {
                    auto* note = Gtk::make_managed<Gtk::Label>(m_note);
                    note->set_xalign(0.0f);
                    note->set_wrap(true);
                    note->add_css_class("jot-keys-note");
                    block->append(*note);
                }
                if (r.action == m_pending_action) {
                    auto* ask = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
                    auto* q = Gtk::make_managed<Gtk::Label>(m_pending_words);
                    q->set_xalign(0.0f);
                    q->set_wrap(true);
                    q->set_hexpand(true);
                    q->add_css_class("jot-keys-ask");
                    ask->append(*q);
                    auto* use = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                                   "shortcuts.use", "Use It Here");
                    use->add_css_class("suggested-action");
                    use->signal_clicked().connect([this]() {
                        const auto o = core::with_key(core::key_overrides(), m_pending_action,
                                                      m_pending_accel, true);
                        log_info("swap " + m_pending_accel + " -> " + m_pending_action);
                        clear_edit_state();
                        m_sig_keys.emit(o);
                    });
                    auto* cancel = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                                      "shortcuts.cancel", "Cancel");
                    cancel->signal_clicked().connect([this]() {
                        clear_edit_state();
                        queue_fill();
                    });
                    ask->append(*cancel);
                    ask->append(*use);
                    block->append(*ask);
                }
            } else if (!r.action.empty()) {
                line->set_tooltip_text(r.action);
            }
            card->append(*block);
            row.widget = block;
            m_rows.push_back(row);
        }
        m_cards.append(*col);
        m_card_widgets.push_back(col->get_parent());   // the FlowBoxChild
    }
    m_edit_bar.set_visible(m_editing);
    filter();
}

void ShortcutsDialog::refresh() { queue_fill(); }

// Rebuilt on idle: a click inside a card must not tear its own row down while
// its handler is still running.
void ShortcutsDialog::queue_fill() {
    if (m_fill_queued) return;
    m_fill_queued = true;
    Glib::signal_idle().connect_once([this]() {
        m_fill_queued = false;
        fill_cards();
    });
}

void ShortcutsDialog::clear_edit_state() {
    m_capture.clear();
    m_pending_action.clear();
    m_pending_accel.clear();
    m_pending_words.clear();
    m_note_action.clear();
    m_note.clear();
}

void ShortcutsDialog::set_editing(bool on) {
    if (m_edit.get_active() != on) { m_edit.set_active(on); return; }   // toggled() comes back here
    m_editing = on;
    clear_edit_state();
    queue_fill();
    log_info(on ? "editing" : "done editing");
}

void ShortcutsDialog::begin_capture(const std::string& action) {
    clear_edit_state();
    m_capture = action;
    queue_fill();
    log_info("waiting for a key for " + action);
}

// A key while a row waits for one. Esc gives up, Backspace leaves the verb with
// no key; anything else is checked by core and either set, or explained, or
// (another verb has it) put to the reader as a swap.
bool ShortcutsDialog::on_capture_key(guint keyval, Gdk::ModifierType state) {
    switch (keyval) {
        case GDK_KEY_Shift_L: case GDK_KEY_Shift_R: case GDK_KEY_Control_L:
        case GDK_KEY_Control_R: case GDK_KEY_Alt_L: case GDK_KEY_Alt_R:
        case GDK_KEY_Super_L: case GDK_KEY_Super_R: case GDK_KEY_Meta_L:
        case GDK_KEY_Meta_R: case GDK_KEY_ISO_Level3_Shift: case GDK_KEY_Caps_Lock:
            return true;   // still waiting for the chord
        default: break;
    }
    const std::string action = m_capture;
    m_capture.clear();
    if (keyval == GDK_KEY_Escape) {
        log_info("capture cancelled");
        queue_fill();
        return true;
    }
    if (keyval == GDK_KEY_BackSpace) {
        log_info("no key for " + action);
        m_sig_keys.emit(core::with_key(core::key_overrides(), action, "", false));
        return true;
    }
    const auto mods = state & Gtk::Accelerator::get_default_mod_mask();
    const std::string accel = core::canonical_accel(
        Gtk::Accelerator::name(gdk_keyval_to_lower(keyval), mods).raw());
    const core::KeyCheck c = core::check_new_key(action, accel);
    log_info("key " + accel + " for " + action + " -> " + std::to_string(static_cast<int>(c.kind)) +
             (c.words.empty() ? "" : " (" + c.words + ")"));
    if (c.kind == core::KeyCheck::Ok) {
        m_sig_keys.emit(core::with_key(core::key_overrides(), action, accel, false));
        return true;
    }
    if (c.kind == core::KeyCheck::Taken) {
        m_pending_action = action;
        m_pending_accel = accel;
        m_pending_words = c.words + " Use it here instead? It is taken from there.";
        queue_fill();
        return true;
    }
    m_note_action = action;
    m_note = c.words;
    queue_fill();
    return true;
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
