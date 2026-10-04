#include "ProjectsPane.hpp"
#include "Log.hpp"
#include "core/Review.hpp"
#include "core/Tasks.hpp"

#include <glibmm/main.h>
#include <gtkmm/enums.h>

#include <ctime>

// ProjectsPane.cpp -- draws core::review_list and core::projects_by_state.
// Writes one thing (Mark Reviewed, straight through core) and decides nothing.

namespace jot {
namespace {

std::string titled(const core::Node* n) {
    if (!n) return {};
    return n->title.empty() ? std::string("Untitled") : n->title;
}

// "3 left, 1 available  ·  Next: Tiles" -- what a review is looking at: is
// there a next step, and is it the right one.
std::string counts_line(const core::NodeSource& src, const core::Node& n, std::int64_t now) {
    const auto c = core::project_counts(src, n.id, now);
    if (c.total == 0) return "No todos in it yet";
    if (c.left == 0)  return "Every todo done — finished?";
    std::string s = std::to_string(c.left) + " left";
    if (c.available != c.left) s += ", " + std::to_string(c.available) + " available";
    if (const core::Node* nx = c.next.empty() ? nullptr : src.find(c.next))
        s += "  ·  Next: " + titled(nx);
    else
        s += "  ·  No next step";
    return s;
}

// "Review today  ·  every 2 weeks" -- the interval only when it is not the
// default, so the common line stays short.
std::string when_line(const core::Node& n, std::int64_t now) {
    std::string s = core::review_when(n, now);
    if (n.task.review.on()) s += "  ·  " + core::repeat_text(n.task.review);
    return s;
}

// "a week", "2 weeks" -- the interval as a span, for "comes back in ...".
std::string interval_words(const core::Repeat& r) {
    const std::string t = core::repeat_text(r);            // "every week", "every 2 weeks"
    const std::string tail = t.size() > 6 ? t.substr(6) : t;
    return r.every == 1 ? "a " + tail : tail;
}

}  // namespace

ProjectsPane::ProjectsPane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL, 0),
      m_modes("projects.modes", Gtk::Orientation::HORIZONTAL, 0),
      m_mode_review("projects.mode_review"),
      m_mode_all("projects.mode_all"),
      m_summary("projects.summary"),
      m_scroll("projects.scroll"),
      m_column("projects.column", Gtk::Orientation::VERTICAL, 4),
      m_empty("projects.empty"),
      m_new("projects.new") {
    // One concept, one widget: two joined buttons, exactly one pressed.
    m_modes.add_css_class("linked");
    m_modes.set_halign(Gtk::Align::CENTER);
    m_modes.set_margin_top(8);
    m_modes.set_margin_bottom(6);
    m_mode_review.set_label("Review");
    m_mode_review.set_tooltip_text("Projects due a look, the longest-waiting first");
    m_mode_all.set_label("All projects");
    m_mode_all.set_tooltip_text("Every project by where it stands");
    m_mode_all.set_group(m_mode_review);
    m_mode_review.set_active(true);
    m_mode_review.signal_toggled().connect([this]() {
        if (m_building || !m_mode_review.get_active()) return;
        set_review_mode(true);
    });
    m_mode_all.signal_toggled().connect([this]() {
        if (m_building || !m_mode_all.get_active()) return;
        set_review_mode(false);
    });
    m_modes.append(m_mode_review);
    m_modes.append(m_mode_all);
    // s037b: + New project, at the end of the same row -- the list and the
    // way to add to it, side by side.
    auto* top = Gtk::make_managed<widgets::Box>(widgets::unregistered, "projects.top",
                                                Gtk::Orientation::HORIZONTAL, 8);
    top->set_halign(Gtk::Align::CENTER);
    top->set_margin_top(8);
    top->set_margin_bottom(6);
    m_modes.set_margin_top(0);
    m_modes.set_margin_bottom(0);
    top->append(m_modes);
    m_new.set_icon_name("list-add-symbolic");
    m_new.set_tooltip_text("New project: a top-level note that is a project from the start. "
                           "Name it, then Ctrl+Shift+N adds its first step.");
    m_new.signal_clicked().connect([this]() { m_sig_new.emit(); });
    top->append(m_new);
    append(*top);

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

void ProjectsPane::set_source(core::NodeSource* src) {
    m_src = src;
    refresh();
}

void ProjectsPane::set_review_mode(bool review) {
    m_review = review;
    m_building = true;
    (review ? m_mode_review : m_mode_all).set_active(true);
    m_building = false;
    refresh();
}

core::NodeId ProjectsPane::first_due() const {
    if (!m_src) return {};
    const auto due = core::review_list(*m_src, static_cast<std::int64_t>(std::time(nullptr)));
    return due.empty() ? core::NodeId{} : due.front();
}

void ProjectsPane::refresh() {
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    while (auto* c = m_column.get_first_child()) m_column.remove(*c);
    m_column.append(m_empty);
    m_empty.set_visible(false);

    // The Review button carries the count either way: "is anything due" is the
    // question you came with, whichever list is up.
    const std::size_t due = m_src ? core::review_list(*m_src, now).size() : 0;
    m_mode_review.set_label(due ? "Review " + std::to_string(due) : std::string("Review"));

    if (!m_src) {
        m_summary.set_text("");
        return;
    }
    if (m_review) fill_review(now);
    else          fill_all(now);
}

// ── rows ────────────────────────────────────────────────────────────────────
void ProjectsPane::add_heading(const std::string& text, const std::string& id, const char* icon) {
    auto* row = Gtk::make_managed<widgets::Box>(widgets::unregistered, "projects.head." + id,
                                                Gtk::Orientation::HORIZONTAL, 6);
    if (icon) {
        auto* im = Gtk::make_managed<widgets::Image>(widgets::unregistered,
                                                     "projects.head_icon." + id);
        im->set_from_icon_name(icon);
        row->append(*im);
    }
    auto* h = Gtk::make_managed<widgets::Label>(widgets::unregistered, "projects.head_text." + id);
    h->set_text(text);
    h->set_xalign(0.0f);
    h->add_css_class("heading");
    row->append(*h);
    const bool first = m_column.get_first_child() == &m_empty && !m_empty.get_next_sibling();
    row->set_margin_top(first ? 0 : 10);
    m_column.append(*row);
}

Gtk::Widget* ProjectsPane::project_row(const core::Node& n, const std::string& line1,
                                       const std::string& line2, bool reviewed_button, bool dim) {
    const core::NodeId id = n.id;
    auto* row = Gtk::make_managed<widgets::Box>(widgets::unregistered, "projects.row." + id,
                                                Gtk::Orientation::HORIZONTAL, 6);
    auto* text = Gtk::make_managed<widgets::Box>(widgets::unregistered, "projects.rowtext." + id,
                                                 Gtk::Orientation::VERTICAL, 0);
    auto* title = Gtk::make_managed<widgets::Label>(widgets::unregistered, "projects.title." + id);
    title->set_text(n.title.empty() ? "(untitled)" : n.title);
    title->set_xalign(0.0f);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    if (!n.title.empty()) title->set_tooltip_text(n.title);
    if (dim) title->add_css_class("dim-label");
    text->append(*title);
    for (const std::string* l : {&line1, &line2}) {
        if (l->empty()) continue;
        auto* s = Gtk::make_managed<widgets::Label>(widgets::unregistered, "projects.sub." + id);
        s->set_text(*l);
        s->set_xalign(0.0f);
        s->set_ellipsize(Pango::EllipsizeMode::END);
        s->set_tooltip_text(*l);
        s->add_css_class("dim-label");
        s->add_css_class("caption");
        text->append(*s);
    }
    auto* go = Gtk::make_managed<widgets::Button>(widgets::unregistered, "projects.goto." + id);
    go->set_has_frame(false);
    go->set_hexpand(true);
    go->set_child(*text);
    go->signal_clicked().connect([this, id]() { m_sig_goto.emit(id); });
    row->append(*go);

    if (reviewed_button) {
        auto* done = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                        "projects.reviewed." + id);
        done->set_icon_name("object-select-symbolic");
        done->set_valign(Gtk::Align::CENTER);
        done->add_css_class("flat");
        done->set_tooltip_text("Reviewed — it comes back in " +
                               interval_words(core::review_every(n.task)) +
                               ".  Ctrl+Shift+R does the same on the selected project.");
        done->signal_clicked().connect([this, id]() {
            if (!m_src) return;
            if (auto lg = log::get(log::Area::Drawer)) lg->info("projects: reviewed {}", id);
            // The model change queues the Shell's refresh; the row leaves on
            // that idle, not inside its own button's signal.
            core::mark_reviewed(*m_src, id, static_cast<std::int64_t>(std::time(nullptr)));
        });
        row->append(*done);
    }
    return row;
}

// ── Review ──────────────────────────────────────────────────────────────────
void ProjectsPane::fill_review(std::int64_t now) {
    const auto due = core::review_list(*m_src, now);
    const auto by  = core::projects_by_state(*m_src);
    const std::size_t live = by.active.size() + by.on_hold.size();

    if (due.empty()) {
        m_summary.set_text("");
        if (live == 0) {
            m_empty.set_text("No projects yet.\n\n"
                             "A note with todos directly under it is a project. Once a "
                             "week it comes up here for a look: is the next step still "
                             "right, is anything stuck, should it go on hold?");
        } else {
            std::string t = "Nothing to review. Every project has had its look.";
            const core::NodeId next = core::next_up(*m_src, now);
            if (const core::Node* n = next.empty() ? nullptr : m_src->find(next))
                t += "\n\n" + core::review_when(*n, now) + ": " + titled(n) + ".";
            m_empty.set_text(t);
        }
        m_empty.set_visible(true);
        return;
    }

    m_summary.set_text(std::to_string(due.size()) + " of " + std::to_string(live) +
                       (live == 1 ? " project" : " projects") +
                       " due a look. Open one, check its next step, then ✓.");
    for (const auto& id : due)
        if (const core::Node* n = m_src->find(id)) {
            std::string l1 = counts_line(*m_src, *n, now);
            if (core::project_state(*n) == core::ProjectState::OnHold) l1 = "On hold  ·  " + l1;
            m_column.append(*project_row(*n, l1, when_line(*n, now), true, false));
        }
    if (auto lg = log::get(log::Area::Drawer)) lg->debug("projects: review {} due", due.size());
}

// ── All projects ────────────────────────────────────────────────────────────
void ProjectsPane::fill_all(std::int64_t now) {
    const auto by = core::projects_by_state(*m_src);
    const std::size_t total =
        by.active.size() + by.on_hold.size() + by.completed.size() + by.dropped.size();
    if (total == 0) {
        m_summary.set_text("");
        m_empty.set_text("No projects yet.\n\n"
                         "A note with todos directly under it is a project. Set its "
                         "state in Note details › Structure › Status.");
        m_empty.set_visible(true);
        return;
    }
    std::string s = std::to_string(by.active.size()) + " active";
    if (!by.on_hold.empty())   s += "  ·  " + std::to_string(by.on_hold.size()) + " on hold";
    if (!by.completed.empty()) s += "  ·  " + std::to_string(by.completed.size()) + " completed";
    if (!by.dropped.empty())   s += "  ·  " + std::to_string(by.dropped.size()) + " dropped";
    m_summary.set_text(s);

    auto live_rows = [&](const std::vector<core::NodeId>& ids, bool held) {
        for (const auto& id : ids)
            if (const core::Node* n = m_src->find(id)) {
                // Held by a parent rather than by itself: say whose, as the drawer does.
                std::string l1 = counts_line(*m_src, *n, now);
                if (held && core::project_state(*n) != core::ProjectState::OnHold) {
                    const core::NodeId by_id = core::stopped_by(*m_src, id);
                    if (const core::Node* b = by_id.empty() ? nullptr : m_src->find(by_id))
                        l1 = "In “" + titled(b) + "”, on hold  ·  " + l1;
                }
                m_column.append(*project_row(*n, l1, when_line(*n, now), false, held));
            }
    };
    if (!by.active.empty()) {
        add_heading("Active  ·  " + std::to_string(by.active.size()), "active",
                    "jot-state-active-symbolic");
        live_rows(by.active, false);
    }
    if (!by.on_hold.empty()) {
        add_heading("On hold  ·  " + std::to_string(by.on_hold.size()), "on_hold",
                    "jot-state-hold-symbolic");
        live_rows(by.on_hold, true);
    }

    // Finished ones fold at the foot, as Tags folds Done: kept, not crowding.
    auto fold = [&](const std::vector<core::NodeId>& ids, const char* word, const char* key,
                    const char* icon, bool& open) {
        if (ids.empty()) return;
        auto* ex = Gtk::make_managed<widgets::Expander>(widgets::unregistered,
                                                        std::string("projects.fold.") + key);
        auto* head = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                     std::string("projects.fold_head.") + key,
                                                     Gtk::Orientation::HORIZONTAL, 6);
        auto* im = Gtk::make_managed<widgets::Image>(widgets::unregistered,
                                                     std::string("projects.fold_icon.") + key);
        im->set_from_icon_name(icon);
        head->append(*im);
        auto* hl = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                     std::string("projects.fold_text.") + key);
        hl->set_text(std::string(word) + "  ·  " + std::to_string(ids.size()));
        hl->add_css_class("heading");
        head->append(*hl);
        ex->set_label_widget(*head);
        ex->set_expanded(open);
        ex->property_expanded().signal_changed().connect([ex, &open]() { open = ex->get_expanded(); });
        ex->set_margin_top(10);
        auto* col = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                    std::string("projects.fold_col.") + key,
                                                    Gtk::Orientation::VERTICAL, 4);
        col->set_margin_top(4);
        for (const auto& id : ids)
            if (const core::Node* n = m_src->find(id)) {
                // Its own state: when. A parent's: whose -- the fix is there.
                std::string l1 = word;
                if (core::effective_state(*m_src, id) == core::project_state(*n)) {
                    if (n->task.finished) l1 += " " + core::format_date(n->task.finished);
                } else {
                    const core::NodeId by_id = core::stopped_by(*m_src, id);
                    if (const core::Node* b = by_id.empty() ? nullptr : m_src->find(by_id))
                        l1 = "In “" + titled(b) + "”";
                }
                col->append(*project_row(*n, l1, {}, false, true));
            }
        ex->set_child(*col);
        m_column.append(*ex);
    };
    fold(by.completed, "Completed", "completed", "jot-state-done-symbolic", m_completed_open);
    fold(by.dropped, "Dropped", "dropped", "jot-state-dropped-symbolic", m_dropped_open);
}

}  // namespace jot
