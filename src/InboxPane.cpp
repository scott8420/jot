#include "InboxPane.hpp"
#include "TaskCard.hpp"
#include "Log.hpp"
#include "core/Inbox.hpp"

#include <gtkmm/cssprovider.h>
#include <gtkmm/enums.h>
#include <gdkmm/display.h>

#include <ctime>

// InboxPane.cpp -- draws core::inbox_members and the verdict on each. Writes
// one thing (Keep: the mark off, through the NodeSource) and waits to be told.

namespace jot {
namespace {

// s029: two text verbs on a 280 px pane left the title three words wide. The
// theme's button padding is sized for a dialog, not for a row; this trims it
// to the text. Once per display, on the DrawerPane / TreePane_dnd pattern.
void install_verb_css() {
    static bool done = false;
    if (done) return;
    auto display = Gdk::Display::get_default();
    if (!display) return;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(".jot-inbox-verb { padding: 2px 5px; min-width: 0; min-height: 0; }");
    gtk_style_context_add_provider_for_display(
        display->gobj(), GTK_STYLE_PROVIDER(css->gobj()),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    done = true;
}

}  // namespace

InboxPane::InboxPane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL, 0),
      m_bar("inbox.bar", Gtk::Orientation::HORIZONTAL, 8),
      m_summary("inbox.summary"),
      m_clean("inbox.clean_up"),
      m_scroll("inbox.scroll"),
      m_column("inbox.column", Gtk::Orientation::VERTICAL, 6),
      m_empty("inbox.empty") {
    install_verb_css();
    m_bar.set_margin_start(12);
    m_bar.set_margin_end(8);
    m_bar.set_margin_top(4);
    m_bar.set_margin_bottom(4);
    m_summary.set_xalign(0.0f);
    m_summary.set_hexpand(true);
    m_summary.set_ellipsize(Pango::EllipsizeMode::END);
    m_summary.add_css_class("dim-label");
    m_summary.add_css_class("caption");
    m_bar.append(m_summary);

    // win.clean-up through the muxer: the Shell owns the verb and greys it when
    // nothing is processed, so this button, the menu item and Ctrl+Shift+K can
    // never disagree about whether there is anything to do.
    m_clean.set_label("Clean Up");
    m_clean.set_action_name("win.clean-up");
    m_clean.set_tooltip_text("Take everything you have filed or finished off the Inbox");
    m_bar.append(m_clean);
    append(m_bar);

    m_column.set_margin(12);
    m_empty.set_wrap(true);
    m_empty.set_xalign(0.0f);
    m_empty.add_css_class("dim-label");
    m_column.append(m_empty);

    m_scroll.set_child(m_column);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    append(m_scroll);
}

void InboxPane::set_source(core::NodeSource* src) {
    m_src = src;
    refresh();
}

// One row: the title (the button that goes to the note), the line under it
// saying how long it has waited or what became of it, and Keep on a row that
// is still waiting.
Gtk::Widget* InboxPane::row(const core::Node& n, std::int64_t now) {
    const core::InboxState st = core::inbox_state(n);
    const core::NodeId id = n.id;

    auto* box = Gtk::make_managed<widgets::Box>(widgets::unregistered, "inbox.row." + id,
                                                Gtk::Orientation::HORIZONTAL, 0);
    // s043: the card shape. A waiting capture has the darker edge (it wants
    // filing); a filed or done one is quiet until Clean Up takes it.
    box->add_css_class("jot-card");
    pickable(*box, id);   // s057b
    box->add_css_class(st == core::InboxState::Waiting ? "st-inbox"
                       : st == core::InboxState::Done  ? "st-done" : "st-note");
    auto* text = Gtk::make_managed<widgets::Box>(widgets::unregistered, "inbox.rowtext." + id,
                                                 Gtk::Orientation::VERTICAL, 0);
    auto* title = Gtk::make_managed<widgets::Label>(widgets::unregistered, "inbox.title." + id);
    title->set_text(n.title.empty() ? "(untitled)" : n.title);
    title->set_xalign(0.0f);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    if (!n.title.empty()) title->set_tooltip_text(n.title);
    if (st != core::InboxState::Waiting) title->add_css_class("dim-label");
    text->append(*title);

    std::string sub;
    switch (st) {
        case core::InboxState::Waiting: {
            const std::string age = core::age_phrase(n.created, now);
            sub = age.empty() ? std::string("Waiting") : "Captured " + age;
            break;
        }
        case core::InboxState::Filed: {
            const core::Node* p = m_src->find(n.parent_id);
            const std::string where = (p && !p->title.empty()) ? p->title : "Untitled";
            sub = "Filed in " + where + "  ·  Clean Up takes it";
            break;
        }
        case core::InboxState::Done:
            sub = "Done  ·  Clean Up takes it";
            break;
    }
    auto* when = Gtk::make_managed<widgets::Label>(widgets::unregistered, "inbox.state." + id);
    when->set_text(sub);
    when->set_xalign(0.0f);
    when->set_ellipsize(Pango::EllipsizeMode::END);
    when->add_css_class("dim-label");
    when->add_css_class("caption");
    text->append(*when);

    auto* go = Gtk::make_managed<widgets::Button>(widgets::unregistered, "inbox.goto." + id);
    go->set_has_frame(false);
    go->set_hexpand(true);
    go->set_child(*text);
    go->signal_clicked().connect([this, id]() { m_sig_goto.emit(id); });
    go->add_css_class("jot-card-go");
    box->append(*go);

    if (st == core::InboxState::Waiting) {
        // s029: the processing verb. First, because filing is the usual answer
        // and Keep the exception. Insensitive on a protected note -- it cannot
        // move, and a picker with nowhere to go is a dead end.
        auto* move = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                        "inbox.move." + id);
        move->set_label("Move\u2026");
        move->set_has_frame(false);
        move->set_valign(Gtk::Align::CENTER);
        move->add_css_class("caption");
        move->add_css_class("jot-inbox-verb");
        move->set_sensitive(!n.protect);
        move->set_tooltip_text(n.protect ? "Protected: it cannot be moved"
                                         : "File it under another note (Ctrl+M)");
        move->signal_clicked().connect([this, id]() { m_sig_move.emit(id); });
        box->append(*move);

        // Text, not an icon: has_icon() lied under Xvfb (s024), and "Keep" says
        // what it does where a tick would only say "something".
        auto* keep = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                        "inbox.keep." + id);
        keep->set_label("Keep");
        keep->set_has_frame(false);
        keep->set_valign(Gtk::Align::CENTER);
        keep->add_css_class("caption");
        keep->add_css_class("jot-inbox-verb");
        keep->set_tooltip_text("It belongs where it is — take it off the Inbox now");
        keep->signal_clicked().connect([this, id]() {
            if (m_src) m_src->set_inbox(id, false);   // the Shell's idle refresh redraws
        });
        box->append(*keep);
    }
    return box;
}

void InboxPane::refresh() {
    while (auto* c = m_column.get_first_child()) m_column.remove(*c);
    m_column.append(m_empty);
    m_empty.set_visible(false);

    if (!m_src) {
        m_summary.set_text("");
        m_empty.set_visible(true);
        return;
    }
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    const auto ids = core::inbox_members(*m_src);
    const auto counts = core::inbox_counts(*m_src);

    for (const auto& id : ids)
        if (const core::Node* n = m_src->find(id)) m_column.append(*row(*n, now));

    std::string s;
    if (counts.waiting) s = std::to_string(counts.waiting) + " waiting";
    if (counts.ready)
        s += (s.empty() ? "" : "  ·  ") + std::to_string(counts.ready) + " processed";
    m_summary.set_text(s.empty() ? "Nothing waiting" : s);

    if (ids.empty()) {
        m_empty.set_text("The Inbox is empty.\n\n"
                         "Captures land here — the capture line, jot --capture, the "
                         "global shortcut, and new notes from jot --list. Move one into "
                         "the tree (Move\u2026, or drag it) or tick it done, and Clean Up "
                         "takes it off. Keep takes off one that belongs where it is.");
        m_empty.set_visible(true);
    }

    if (auto lg = log::get(log::Area::Drawer))
        lg->debug("inbox: {} member(s), {} waiting, {} processed", ids.size(),
                  counts.waiting, counts.ready);
}

}  // namespace jot
