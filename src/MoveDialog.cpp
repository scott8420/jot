#include "MoveDialog.hpp"
#include "core/Feeders.hpp"
#include "Log.hpp"
#include "Registry.hpp"

#include <gtkmm/box.h>
#include <gtkmm/enums.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/headerbar.h>
#include <gdk/gdkkeysyms.h>
#include <glibmm/markup.h>

#include <algorithm>

// MoveDialog.cpp -- "Move to...". See the header: core/Filing decides, this
// draws and reports.

namespace jot {

MoveDialog::MoveDialog(Gtk::Window& parent, const core::NodeSource& src,
                       const core::NodeId& moving, std::vector<core::NodeId> recent,
                       Done done, Purpose purpose)
    : m_src(src),
      m_moving(moving),
      m_recent(std::move(recent)),
      m_done(std::move(done)),
      m_purpose(purpose),
      m_heading("move.heading"),
      m_search("move.search"),
      m_scroll("move.scroll"),
      m_list("move.list"),
      m_footer("move.footer"),
      m_accept("move.accept") {
    set_name("shell.move");
    registry::add("shell.move", this);

    const bool feeding = purpose == Purpose::Feed;   // s058
    set_title(feeding ? "Feeds" : "Move to");
    set_modal(true);
    set_transient_for(parent);
    set_default_size(420, 480);
    set_hide_on_close(true);   // held in a unique_ptr by the Shell

    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(true);
    set_titlebar(*hb);

    auto* page = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, "move.page", Gtk::Orientation::VERTICAL, 10);
    page->set_margin(18);

    const core::Node* n = m_src.find(m_moving);
    const std::string title = (n && !n->title.empty()) ? n->title : std::string("Untitled");
    m_heading.set_markup(feeding ? "<b>“" + Glib::Markup::escape_text(title) + "” feeds…</b>"
                                 : "<b>Move “" + Glib::Markup::escape_text(title) + "” to…</b>");
    m_heading.set_xalign(0.0f);
    m_heading.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    page->append(m_heading);

    m_search.set_placeholder_text("Type to find a note");
    m_search.set_icon_from_icon_name("system-search-symbolic", Gtk::Entry::IconPosition::PRIMARY);
    m_search.signal_changed().connect([this]() { rebuild(); });
    m_search.signal_activate().connect([this]() { accept(); });
    // Up / Down walk the list while the cursor stays in the search line --
    // CAPTURE, because the entry would otherwise take the keys for itself
    // (s025: a key the widget already binds needs CAPTURE).
    auto keys = Gtk::EventControllerKey::create();
    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    keys->signal_key_pressed().connect(
        [this](guint key, guint, Gdk::ModifierType) -> bool {
            if (key == GDK_KEY_Down || key == GDK_KEY_KP_Down) { step(+1); return true; }
            if (key == GDK_KEY_Up || key == GDK_KEY_KP_Up)     { step(-1); return true; }
            return false;
        },
        false);
    m_search.add_controller(keys);
    page->append(m_search);

    m_list.set_selection_mode(Gtk::SelectionMode::BROWSE);
    m_list.set_activate_on_single_click(true);
    m_list.add_css_class("navigation-sidebar");
    m_list.signal_row_activated().connect([this](Gtk::ListBoxRow*) { accept(); });
    m_scroll.set_child(m_list);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    m_scroll.add_css_class("frame");
    page->append(m_scroll);

    m_footer.set_xalign(0.0f);
    m_footer.set_wrap(true);
    m_footer.add_css_class("dim-label");
    m_footer.add_css_class("caption");
    page->append(m_footer);

    auto* buttons = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, "move.buttons", Gtk::Orientation::HORIZONTAL, 8);
    buttons->set_halign(Gtk::Align::END);
    auto* cancel = Gtk::make_managed<widgets::Button>(widgets::unregistered, "move.cancel",
                                                      "Cancel");
    cancel->signal_clicked().connect([this]() { set_visible(false); });
    buttons->append(*cancel);
    m_accept.set_label(feeding ? "Choose" : "Move");
    m_accept.add_css_class("suggested-action");
    m_accept.signal_clicked().connect([this]() { accept(); });
    buttons->append(m_accept);
    page->append(*buttons);

    // Escape anywhere in the window is Cancel.
    auto esc = Gtk::EventControllerKey::create();
    esc->signal_key_pressed().connect(
        [this](guint key, guint, Gdk::ModifierType) -> bool {
            if (key != GDK_KEY_Escape) return false;
            set_visible(false);
            return true;
        },
        false);
    add_controller(esc);

    set_child(*page);
    rebuild();
    m_search.grab_focus();
}

MoveDialog::~MoveDialog() { registry::remove(this); }

void MoveDialog::add_header(const std::string& text) {
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_selectable(false);
    row->set_activatable(false);
    row->set_focusable(false);
    auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered, "move.header", text);
    l->set_xalign(0.0f);
    l->set_margin_top(6);
    l->add_css_class("dim-label");
    l->add_css_class("caption-heading");
    row->set_child(*l);
    m_list.append(*row);
}

void MoveDialog::add_target(const core::MoveTarget& t) {
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_name("move.row." + (t.id.empty() ? std::string("top") : t.id));
    auto* box = Gtk::make_managed<widgets::Box>(widgets::unregistered, "move.rowbox",
                                                Gtk::Orientation::VERTICAL, 0);
    box->set_valign(Gtk::Align::CENTER);   // a one-line place sits mid-row, not on top
    auto* title = Gtk::make_managed<widgets::Label>(widgets::unregistered, "move.rowtitle",
                                                    t.title);
    title->set_xalign(0.0f);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    if (t.id.empty()) title->add_css_class("heading");
    box->append(*title);
    if (!t.path.empty()) {
        auto* path = Gtk::make_managed<widgets::Label>(widgets::unregistered, "move.rowpath",
                                                       "in " + t.path);
        path->set_xalign(0.0f);
        path->set_ellipsize(Pango::EllipsizeMode::START);   // the near end is the useful end
        path->add_css_class("dim-label");
        path->add_css_class("caption");
        box->append(*path);
    }
    row->set_child(*box);
    m_list.append(*row);
    m_rows.emplace_back(row, t.id);
}

// The list, from the query. Rows are cheap and the list is capped, so a full
// rebuild per keystroke is the simple shape and the right one.
void MoveDialog::rebuild() {
    while (auto* c = m_list.get_first_child()) m_list.remove(*c);
    m_rows.clear();

    const std::string q = m_search.get_text();
    if (m_purpose == Purpose::Feed) {   // s058: deadlines first, then every note
        const auto targets = core::feed_targets(m_src, m_moving, q);
        std::size_t shown = 0;
        bool dl_head = false, all_head = false;
        for (const auto& t : targets) {
            if (shown == kMaxRows) break;
            if (t.deadline && !dl_head) { add_header("Deadlines"); dl_head = true; }
            if (!t.deadline && !all_head && dl_head) { add_header("All notes"); all_head = true; }
            add_target(core::MoveTarget{t.id, t.title, t.path});
            ++shown;
        }
        m_footer.set_text(targets.empty() ? (q.empty() ? "There is nothing else here to feed."
                                                       : "No note matches “" + q + "”.")
                                          : "Enter: it feeds the highlighted note.");
        if (!m_rows.empty()) m_list.select_row(*m_rows.front().first);
        m_accept.set_sensitive(!m_rows.empty());
        return;
    }
    const auto all = core::move_targets(m_src, m_moving, q);

    std::size_t drawn = 0;
    if (q.empty()) {
        const auto recent = core::recent_targets(m_src, m_moving, m_recent);
        if (!recent.empty()) {
            add_header("Recent");
            for (const auto& t : recent) add_target(t);
            add_header("All notes");
        }
    }
    for (const auto& t : all) {
        if (drawn == kMaxRows) break;
        add_target(t);
        ++drawn;
    }

    if (all.empty()) {
        m_footer.set_text(q.empty() ? "There is nowhere else this note can go."
                                    : "No note matches “" + q + "”.");
    } else if (all.size() > drawn) {
        m_footer.set_text("Showing " + std::to_string(drawn) + " of " +
                          std::to_string(all.size()) + " — type more to narrow it.");
    } else {
        m_footer.set_text("Enter files it under the highlighted note.");
    }

    if (!m_rows.empty()) m_list.select_row(*m_rows.front().first);
    m_accept.set_sensitive(!m_rows.empty());
}

void MoveDialog::step(int delta) {
    if (m_rows.empty()) return;
    const Gtk::ListBoxRow* cur = m_list.get_selected_row();
    int at = 0;
    for (std::size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].first == cur) { at = static_cast<int>(i); break; }
    at = std::clamp(at + delta, 0, static_cast<int>(m_rows.size()) - 1);
    m_list.select_row(*m_rows[static_cast<std::size_t>(at)].first);
    // Keep it on screen: the row grabs nothing (the cursor stays in the
    // search line), so scroll by hand.
    // A row's allocation is relative to its parent -- the list -- which is
    // the scrolled content, so it is already in the adjustment's units.
    Gtk::ListBoxRow* row = m_rows[static_cast<std::size_t>(at)].first;
    const auto a = row->get_allocation();
    const double y = a.get_y();
    const double h = a.get_height();
    auto adj = m_scroll.get_vadjustment();
    if (y < adj->get_value()) adj->set_value(y);
    else if (y + h > adj->get_value() + adj->get_page_size())
        adj->set_value(y + h - adj->get_page_size());
}

void MoveDialog::accept() {
    const Gtk::ListBoxRow* cur = m_list.get_selected_row();
    for (const auto& [row, id] : m_rows) {
        if (row != cur) continue;
        if (auto lg = log::get(log::Area::Shell))
            lg->info("move to: {} -> {}", m_moving, id.empty() ? std::string("(top level)") : id);
        set_visible(false);
        if (m_done) m_done(id);
        return;
    }
}

}  // namespace jot
