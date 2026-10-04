#include "SearchPane.hpp"
#include "Log.hpp"

#include <gtkmm/enums.h>

// SearchPane.cpp -- draws core::search. Writes nothing.

namespace jot {
namespace {

constexpr std::size_t kCap = 200;   // rows; the summary says when it stopped

std::string titled(const core::Node* n) {
    if (!n) return {};
    return n->title.empty() ? std::string("Untitled") : n->title;
}

}  // namespace

SearchPane::SearchPane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL, 0),
      m_summary("search.summary"),
      m_scroll("search.scroll"),
      m_column("search.column", Gtk::Orientation::VERTICAL, 2) {
    m_summary.set_xalign(0.0f);
    m_summary.set_wrap(true);
    m_summary.set_margin_start(12);
    m_summary.set_margin_end(12);
    m_summary.set_margin_bottom(4);
    m_summary.add_css_class("dim-label");
    m_summary.add_css_class("caption");
    append(m_summary);

    m_column.set_margin(8);
    m_column.set_margin_top(2);
    m_scroll.set_child(m_column);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    append(m_scroll);
}

void SearchPane::set_source(core::NodeSource* src) {
    m_src = src;
    refresh();
}

void SearchPane::set_query(const std::string& q) {
    m_query = q;
    refresh();
}

bool SearchPane::open_first() {
    if (m_hits.empty()) return false;
    const auto& h = m_hits.front();
    m_sig_open.emit(h.id, h.cp_start, h.cp_len);
    return true;
}

Gtk::Widget* SearchPane::hit_row(const core::SearchHit& h) {
    const core::Node* n = m_src->find(h.id);
    auto* text = Gtk::make_managed<widgets::Box>(widgets::unregistered, "search.rowtext." + h.id,
                                                 Gtk::Orientation::VERTICAL, 0);
    auto* title = Gtk::make_managed<widgets::Label>(widgets::unregistered, "search.title." + h.id);
    title->set_text(titled(n));
    title->set_xalign(0.0f);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    if (h.in_name) title->add_css_class("heading");   // the name is why it is here
    text->append(*title);

    std::string where = (!n || n->parent_id.empty()) ? std::string("Top level")
                                                     : "In " + titled(m_src->find(n->parent_id));
    auto* w = Gtk::make_managed<widgets::Label>(widgets::unregistered, "search.where." + h.id);
    w->set_text(where);
    w->set_xalign(0.0f);
    w->set_ellipsize(Pango::EllipsizeMode::END);
    w->add_css_class("dim-label");
    w->add_css_class("caption");
    text->append(*w);

    if (!h.snippet.empty()) {
        auto* s = Gtk::make_managed<widgets::Label>(widgets::unregistered, "search.snippet." + h.id);
        s->set_text(h.snippet);
        s->set_xalign(0.0f);
        s->set_ellipsize(Pango::EllipsizeMode::END);
        s->set_tooltip_text(h.snippet);
        s->add_css_class("caption");
        text->append(*s);
    }

    auto* go = Gtk::make_managed<widgets::Button>(widgets::unregistered, "search.goto." + h.id);
    go->set_has_frame(false);
    go->set_child(*text);
    const core::NodeId id = h.id;
    const int at = h.cp_start, len = h.cp_len;
    go->signal_clicked().connect([this, id, at, len]() { m_sig_open.emit(id, at, len); });
    return go;
}

void SearchPane::refresh() {
    while (auto* c = m_column.get_first_child()) m_column.remove(*c);
    m_hits.clear();
    if (!m_src || !core::query_active(m_query)) {
        m_summary.set_text("");
        return;
    }
    // s038b. A heading line when the query is more than words: the saved
    // name if it is one, and how jot reads it -- so a filter typed by hand
    // shows what it was taken to mean.
    std::string head;
    if (m_persp)
        if (const core::Perspective* p = core::perspective_for(*m_persp, m_query))
            head = "★ " + p->name;
    const std::string reads = core::describe_query(m_query);
    if (!reads.empty()) head += head.empty() ? reads : "  ·  " + reads;
    if (!head.empty()) head += "\n";

    m_hits = core::search(*m_src, m_query, kCap);
    if (m_hits.empty()) {
        std::string q = m_query;   // as typed, minus the edge spaces a quote would show
        while (!q.empty() && q.front() == ' ') q.erase(q.begin());
        while (!q.empty() && q.back() == ' ') q.pop_back();
        m_summary.set_text(head + (reads.empty() ? "No note has every word of “" + q + "”.  Esc clears."
                                                 : std::string("Nothing matches.  Esc clears.")));
        return;
    }
    std::string s = m_hits.size() == 1 ? std::string("1 note") : std::to_string(m_hits.size()) + " notes";
    if (m_hits.size() == kCap) {
        const std::size_t all = core::search_count(*m_src, m_query);
        if (all > kCap) s = "The first " + std::to_string(kCap) + " of " + std::to_string(all) + " notes";
    }
    m_summary.set_text(head + s + "  ·  Enter opens the first; Esc goes back to the tree");
    for (const auto& h : m_hits) m_column.append(*hit_row(h));
    if (auto lg = log::get(log::Area::Shell))
        lg->debug("search '{}': {} hits", m_query, m_hits.size());
}

}  // namespace jot
