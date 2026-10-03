#include "TagsPane.hpp"
#include "Log.hpp"
#include "core/Tags.hpp"
#include "core/Tasks.hpp"

#include <glibmm/main.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/enums.h>
#include <gdkmm/display.h>

#include <ctime>

// TagsPane.cpp -- draws core::tag_list as chips and core::tag_members as rows.
// Writes one thing (a tick, straight through the NodeSource) and decides nothing.

namespace jot {
namespace {

// Chips are small: the theme's button padding is sized for a dialog. Once per
// display, on the InboxPane pattern.
void install_chip_css() {
    static bool done = false;
    if (done) return;
    auto display = Gdk::Display::get_default();
    if (!display) return;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        ".jot-tag-chip { padding: 1px 8px; min-height: 0; border-radius: 999px; }\n"
        ".jot-tag-implied { font-style: italic; }\n");
    gtk_style_context_add_provider_for_display(
        display->gobj(), GTK_STYLE_PROVIDER(css->gobj()),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    done = true;
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string titled(const core::Node* n) {
    if (!n) return {};
    return n->title.empty() ? std::string("Untitled") : n->title;
}

// Where a row lives: its parent's title. The context view gathers rows from
// all over the tree, so "which project is this step in" is the first question.
std::string where(const core::NodeSource& src, const core::Node& n) {
    if (n.parent_id.empty()) return "Top level";
    return "In " + titled(src.find(n.parent_id));
}

}  // namespace

TagsPane::TagsPane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL, 0),
      m_search("tags.search"),
      m_nomatch("tags.nomatch"),
      m_chip_scroll("tags.chip_scroll"),
      m_chips("tags.chips"),
      m_summary("tags.summary"),
      m_scroll("tags.scroll"),
      m_column("tags.column", Gtk::Orientation::VERTICAL, 4),
      m_empty("tags.empty") {
    install_chip_css();

    // s036. The search sits above the chips it narrows. Narrowing only redraws
    // the chips: the rows below belong to the PICK, and typing is not picking.
    m_search.set_placeholder_text("Find a tag");
    m_search.set_tooltip_text("Type to narrow the tags. Enter picks the one that matches; Esc clears.");
    m_search.set_margin_start(10);
    m_search.set_margin_end(10);
    m_search.set_margin_top(8);
    m_search.set_margin_bottom(6);
    m_search.signal_search_changed().connect([this]() {
        m_query = core::tag_query(m_search.get_text().raw());
        draw_chips();
    });
    m_search.signal_activate().connect([this]() {
        const std::string key = core::tag_pick(m_list, m_search.get_text().raw());
        if (auto lg = log::get(log::Area::Drawer))
            lg->debug("tags: search '{}' Enter -> {}", m_search.get_text().raw(),
                      key.empty() ? std::string("(no single match)") : "#" + key);
        if (key.empty()) { m_search.error_bell(); return; }
        m_key = key;
        refresh();
    });
    m_search.signal_stop_search().connect([this]() { m_search.set_text(""); });
    append(m_search);

    m_nomatch.set_xalign(0.0f);
    m_nomatch.set_wrap(true);
    m_nomatch.set_margin_start(12);
    m_nomatch.set_margin_end(12);
    m_nomatch.set_margin_bottom(6);
    m_nomatch.add_css_class("dim-label");
    m_nomatch.set_visible(false);
    append(m_nomatch);

    m_chips.set_selection_mode(Gtk::SelectionMode::NONE);
    m_chips.set_homogeneous(false);
    m_chips.set_max_children_per_line(30);
    m_chips.set_row_spacing(4);
    m_chips.set_column_spacing(4);
    m_chips.set_margin_start(10);
    m_chips.set_margin_end(10);
    m_chips.set_margin_top(2);
    m_chips.set_margin_bottom(6);
    m_chips.set_valign(Gtk::Align::START);

    // A folder with sixty tags must not push the rows off the pane: the chips
    // scroll inside a band that grows with them up to a cap.
    m_chip_scroll.set_child(m_chips);
    m_chip_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_chip_scroll.set_propagate_natural_height(true);
    m_chip_scroll.set_max_content_height(150);
    append(m_chip_scroll);

    m_summary.set_xalign(0.0f);
    m_summary.set_wrap(true);
    m_summary.set_margin_start(12);
    m_summary.set_margin_end(12);
    m_summary.set_margin_bottom(4);
    m_summary.add_css_class("dim-label");
    m_summary.add_css_class("caption");
    append(m_summary);

    m_column.set_margin(12);
    m_column.set_margin_top(4);
    m_empty.set_wrap(true);
    m_empty.set_xalign(0.0f);
    m_empty.add_css_class("dim-label");
    m_column.append(m_empty);

    m_scroll.set_child(m_column);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    append(m_scroll);
}

void TagsPane::set_source(core::NodeSource* src) {
    m_src = src;
    refresh();
}

void TagsPane::focus_search() {
    if (m_search.get_visible()) m_search.grab_focus();
}

void TagsPane::select(const std::string& tag) {
    m_key = core::tag_key(tag);
    refresh();
}

void TagsPane::refresh() {
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    fill_chips(now);
    fill_members(now);
}

// ── the chips ───────────────────────────────────────────────────────────────
void TagsPane::fill_chips(std::int64_t now) {
    m_list = m_src ? core::tag_list(*m_src, now) : std::vector<core::TagInfo>{};

    // A picked tag that no longer exists anywhere is let go, rather than left
    // showing an empty filter with no chip pressed to say why.
    bool found = m_key.empty();
    for (const auto& t : m_list) found = found || t.key == m_key;
    if (!found) m_key.clear();

    draw_chips();
}

void TagsPane::draw_chips() {
    m_building = true;
    while (auto* c = m_chips.get_first_child()) m_chips.remove(*c);

    std::size_t shown = 0, matched = 0;
    for (const auto& t : m_list) {
        // s036: the query narrows; the pressed chip stays whatever is typed.
        const bool hit = core::tag_matches(t.key, m_query);
        if (hit) ++matched;
        if (t.key != m_key && !hit) continue;
        ++shown;
        const std::size_t rows = t.open + t.notes;
        auto* chip = Gtk::make_managed<widgets::ToggleButton>(widgets::unregistered,
                                                              "tags.chip." + t.key);
        chip->set_label("#" + t.name + "  " + std::to_string(rows));
        chip->set_halign(Gtk::Align::START);
        chip->add_css_class("jot-tag-chip");
        chip->add_css_class("caption");
        if (!t.written) chip->add_css_class("jot-tag-implied");
        std::string tip = plural(t.open, "todo", "todos") + " to do";
        if (t.open) tip += " (" + std::to_string(t.available) + " available now)";
        tip += "  ·  " + plural(t.notes, "note", "notes");
        if (!t.written) tip += "\nOnly written nested: #" + t.name + "/…";
        else if (t.key.find('/') != std::string::npos)
            tip += "\nNested: picking #" + t.name.substr(0, t.name.rfind('/')) +
                   " shows these too";
        chip->set_tooltip_text(tip);
        chip->set_active(t.key == m_key);   // BEFORE the handler: set_active emits
        const std::string key = t.key;
        chip->signal_toggled().connect([this, key, chip]() {
            if (m_building) return;
            // Pressing the pressed chip lets it go; pressing another moves the
            // pick. Rebuilt on an idle: the chip that emitted is about to be
            // destroyed by remove_all(), and must not be destroyed inside its
            // own signal.
            m_key = chip->get_active() ? key : std::string();
            Glib::signal_idle().connect_once([this]() { refresh(); });
        });
        m_chips.append(*chip);
    }
    m_search.set_visible(!m_list.empty());
    m_chip_scroll.set_visible(shown > 0);
    // "No match" whenever nothing MATCHES -- the pressed chip still showing is
    // the pick, not an answer to what was typed.
    m_nomatch.set_visible(!m_list.empty() && matched == 0);
    if (matched == 0 && !m_list.empty())
        m_nomatch.set_text("No tag matches \u201c" + m_query + "\u201d.  Esc clears.");
    m_building = false;
}

// ── the rows ────────────────────────────────────────────────────────────────
void TagsPane::add_heading(const std::string& text, const std::string& id) {
    auto* h = Gtk::make_managed<widgets::Label>(widgets::unregistered, "tags.head." + id);
    h->set_text(text);
    h->set_xalign(0.0f);
    h->add_css_class("heading");
    h->set_margin_top(m_column.get_first_child() == &m_empty &&
                              !m_empty.get_next_sibling() ? 0 : 10);
    m_column.append(*h);
}

Gtk::Widget* TagsPane::todo_row(const core::Node& n, const std::string& sub, bool dim) {
    const core::NodeId id = n.id;
    auto* row = Gtk::make_managed<widgets::Box>(widgets::unregistered, "tags.row." + id,
                                                Gtk::Orientation::HORIZONTAL, 8);
    auto* tick = Gtk::make_managed<widgets::CheckButton>(widgets::unregistered,
                                                         "tags.tick." + id);
    tick->set_valign(Gtk::Align::CENTER);
    tick->set_active(n.task.done);          // BEFORE the handler: set_active emits
    tick->signal_toggled().connect([this, id, tick]() {
        if (m_src) m_src->set_done(id, tick->get_active());   // the Shell's idle refresh redraws
    });
    row->append(*tick);

    auto* text = Gtk::make_managed<widgets::Box>(widgets::unregistered, "tags.rowtext." + id,
                                                 Gtk::Orientation::VERTICAL, 0);
    auto* title = Gtk::make_managed<widgets::Label>(widgets::unregistered, "tags.title." + id);
    title->set_text(n.title.empty() ? "(untitled)" : n.title);
    title->set_xalign(0.0f);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    if (!n.title.empty()) title->set_tooltip_text(n.title);
    if (dim) title->add_css_class("dim-label");
    text->append(*title);
    auto* s = Gtk::make_managed<widgets::Label>(widgets::unregistered, "tags.sub." + id);
    s->set_text(sub);
    s->set_xalign(0.0f);
    s->set_ellipsize(Pango::EllipsizeMode::END);
    s->add_css_class("dim-label");
    s->add_css_class("caption");
    text->append(*s);

    // The title is the button, not the row: the tick has to stay clickable.
    auto* go = Gtk::make_managed<widgets::Button>(widgets::unregistered, "tags.goto." + id);
    go->set_has_frame(false);
    go->set_hexpand(true);
    go->set_child(*text);
    go->signal_clicked().connect([this, id]() { m_sig_goto.emit(id); });
    row->append(*go);
    return row;
}

Gtk::Widget* TagsPane::note_row(const core::Node& n) {
    const core::NodeId id = n.id;
    auto* text = Gtk::make_managed<widgets::Box>(widgets::unregistered, "tags.rowtext." + id,
                                                 Gtk::Orientation::VERTICAL, 0);
    auto* title = Gtk::make_managed<widgets::Label>(widgets::unregistered, "tags.title." + id);
    title->set_text(n.title.empty() ? "(untitled)" : n.title);
    title->set_xalign(0.0f);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    if (!n.title.empty()) title->set_tooltip_text(n.title);
    text->append(*title);
    auto* s = Gtk::make_managed<widgets::Label>(widgets::unregistered, "tags.sub." + id);
    s->set_text(where(*m_src, n));
    s->set_xalign(0.0f);
    s->set_ellipsize(Pango::EllipsizeMode::END);
    s->add_css_class("dim-label");
    s->add_css_class("caption");
    text->append(*s);

    auto* go = Gtk::make_managed<widgets::Button>(widgets::unregistered, "tags.goto." + id);
    go->set_has_frame(false);
    go->set_hexpand(true);
    go->set_child(*text);
    go->signal_clicked().connect([this, id]() { m_sig_goto.emit(id); });
    return go;
}

void TagsPane::fill_members(std::int64_t now) {
    while (auto* c = m_column.get_first_child()) m_column.remove(*c);
    m_column.append(m_empty);
    m_empty.set_visible(false);

    if (!m_src) {
        m_summary.set_text("");
        m_empty.set_visible(true);
        return;
    }
    if (m_list.empty()) {
        m_summary.set_text("");
        m_empty.set_text("No tags yet.\n\n"
                         "Type #errands, #home or #waiting-for anywhere in a note's text and "
                         "it becomes a tag. Pick one here to see every todo you can do "
                         "there and every note that mentions it.\n\n"
                         "Nest them with a slash: #home/garden sits under #home.");
        m_empty.set_visible(true);
        return;
    }
    if (m_key.empty()) {
        m_summary.set_text("");
        m_empty.set_text("Pick a tag above to see its todos and notes. "
                         "Press it again to let it go.");
        m_empty.set_visible(true);
        return;
    }

    const auto mem = core::tag_members(*m_src, m_key, now);

    std::string s;
    auto part = [&](const std::string& p) { s += (s.empty() ? "" : "  ·  ") + p; };
    if (!mem.available.empty() || !mem.waiting.empty() || mem.notes.empty())
        part(std::to_string(mem.available.size()) + " available");
    if (!mem.waiting.empty()) part(std::to_string(mem.waiting.size()) + " waiting");
    if (!mem.notes.empty())   part(plural(mem.notes.size(), "note", "notes"));
    if (mem.finished)         part(std::to_string(mem.finished) + " done");
    m_summary.set_text(s);

    if (!mem.available.empty()) {
        add_heading("Available", "available");
        for (const auto& id : mem.available)
            if (const core::Node* n = m_src->find(id)) {
                std::string sub = where(*m_src, *n);
                const std::int64_t due = core::effective_due(*m_src, id);
                if (due) sub += (due < now ? "  ·  Overdue " : "  ·  Due ") +
                                core::format_date(due);
                if (n->task.flagged) sub += "  ·  Flagged";
                m_column.append(*todo_row(*n, sub, false));
            }
    }
    if (!mem.waiting.empty()) {
        add_heading("Waiting", "waiting");
        for (const auto& id : mem.waiting)
            if (const core::Node* n = m_src->find(id))
                m_column.append(*todo_row(*n, core::waiting_reason(*m_src, id, now) +
                                                  "  ·  " + where(*m_src, *n),
                                          true));
    }
    if (!mem.notes.empty()) {
        add_heading("Notes", "notes");
        for (const auto& id : mem.notes)
            if (const core::Node* n = m_src->find(id)) m_column.append(*note_row(*n));
    }
    // s035b: done and dropped, folded away at the foot -- nothing carrying
    // the tag is left out, and what is finished does not crowd what is not.
    if (!mem.done.empty()) {
        auto* ex = Gtk::make_managed<widgets::Expander>(widgets::unregistered, "tags.done");
        auto* head = Gtk::make_managed<widgets::Label>(widgets::unregistered, "tags.head.done");
        head->set_text("Done  \u00b7  " + std::to_string(mem.done.size()));
        head->add_css_class("heading");
        ex->set_label_widget(*head);
        ex->set_expanded(m_done_open);
        ex->property_expanded().signal_changed().connect(
            [this, ex]() { m_done_open = ex->get_expanded(); });
        ex->set_margin_top(10);
        auto* col = Gtk::make_managed<widgets::Box>(widgets::unregistered, "tags.done.column",
                                                    Gtk::Orientation::VERTICAL, 4);
        col->set_margin_top(4);
        for (const auto& id : mem.done)
            if (const core::Node* n = m_src->find(id)) {
                const bool dropped = core::availability(*m_src, id, now) == core::Avail::Dropped;
                std::string sub = dropped ? "Dropped" : "Done";
                if (!dropped && n->task.finished)
                    sub += " " + core::format_date(n->task.finished);
                col->append(*todo_row(*n, sub + "  \u00b7  " + where(*m_src, *n), true));
            }
        ex->set_child(*col);
        m_column.append(*ex);
    }

    if (auto lg = log::get(log::Area::Drawer))
        lg->debug("tags: #{} -> {} available, {} waiting, {} notes, {} finished", m_key,
                  mem.available.size(), mem.waiting.size(), mem.notes.size(), mem.finished);
}

}  // namespace jot
