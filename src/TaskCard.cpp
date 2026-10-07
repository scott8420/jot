#include "TaskCard.hpp"
#include "core/RowLook.hpp"
#include "core/Tasks.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/enums.h>
#include <algorithm>

namespace jot {
namespace {

// A colour is never the only carrier of a meaning (it fails in a greyscale
// screenshot, for a colour-blind reader, and in a theme jot did not pick). The
// grey states have no chip to say them, so they get a word.
std::string state_word(core::RowState s) {
    switch (s) {
    case core::RowState::OnHold:   return "On hold";
    case core::RowState::Waiting:  return "Waiting";
    case core::RowState::Deferred: return "Deferred";
    default:                       return {};
    }
}

}  // namespace

Gtk::Widget* task_card(core::NodeSource& src, const core::Node& n, std::int64_t now,
                       const CardOpts& opts,
                       std::function<void(const core::NodeId&)> on_open) {
    const core::RowLook look = core::row_look(src, n, now);
    const std::string st = std::string("st-") + core::row_state_word(look.state);
    const core::NodeId id = n.id;
    const std::string& px = opts.prefix;

    auto* card = Gtk::make_managed<widgets::Box>(widgets::unregistered, px + ".card." + id,
                                                 Gtk::Orientation::HORIZONTAL, 10);
    card->add_css_class("jot-card");
    card->add_css_class(st);
    pickable(*card, id);   // s057b
    if (opts.dim) card->add_css_class("jot-card-dim");

    auto* tick = Gtk::make_managed<widgets::CheckButton>(widgets::unregistered,
                                                         px + ".tick." + id);
    tick->add_css_class("jot-tick");
    tick->set_valign(Gtk::Align::CENTER);
    tick->set_active(n.task.done);          // BEFORE the handler: set_active emits
    core::NodeSource* s = &src;
    tick->signal_toggled().connect([s, id, tick]() { s->set_done(id, tick->get_active()); });
    card->append(*tick);

    auto* body = Gtk::make_managed<widgets::Box>(widgets::unregistered, px + ".cardbody." + id,
                                                 Gtk::Orientation::VERTICAL, 2);

    // ── the top line: title, then flag and due on the right ───────────────
    auto* top = Gtk::make_managed<widgets::Box>(widgets::unregistered, px + ".cardtop." + id,
                                                Gtk::Orientation::HORIZONTAL, 6);
    auto* title = Gtk::make_managed<widgets::Label>(widgets::unregistered, px + ".title." + id);
    title->set_text(n.title.empty() ? "(untitled)" : n.title);
    title->set_xalign(0.0f);
    title->set_hexpand(true);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    title->add_css_class("jot-card-title");
    if (!n.title.empty()) title->set_tooltip_text(n.title);
    top->append(*title);

    if (look.flagged) {
        auto* flag = Gtk::make_managed<widgets::Label>(widgets::unregistered, px + ".flag." + id);
        flag->set_text("⚑");
        flag->add_css_class("jot-flag");
        flag->set_tooltip_text(n.task.flagged ? "Flagged" : "Flagged (from a parent)");
        top->append(*flag);
    }
    if (!look.due.empty()) {
        auto* due = Gtk::make_managed<widgets::Label>(widgets::unregistered, px + ".due." + id);
        due->set_text(look.due);
        due->add_css_class("jot-chip");
        due->add_css_class("jot-due");
        due->add_css_class(st);
        due->set_valign(Gtk::Align::CENTER);
        const std::string full = core::format_date(core::effective_due(src, id));
        due->set_tooltip_text("Due " + full + (look.due_inherited ? " (from a parent)" : ""));
        top->append(*due);
    }
    body->append(*top);

    // ── the quiet line: where it lives, why it waits, how long, how often ──
    std::string meta;
    auto add = [&](const std::string& part) {
        if (part.empty()) return;
        if (!meta.empty()) meta += "  ·  ";
        meta += part;
    };
    add(opts.note.empty() ? state_word(look.state) : opts.note);
    if (!look.done_when.empty()) add("\u2611 " + look.done_when);   // s054: done-when's count
    if (!look.runway.empty())    add("\u23f3 " + look.runway);      // s055: the deadline's runway
    if (!look.estimate.empty()) add("⏱ " + look.estimate);
    if (!look.repeat.empty())                                        // s057: and its record
        add("↻ " + look.repeat + (look.routine.empty() ? std::string{} : "  ·  " + look.routine));

    const bool want_project = opts.show_project && !look.project.empty();
    if (want_project || !meta.empty()) {
        auto* line = Gtk::make_managed<widgets::Box>(widgets::unregistered, px + ".cardmeta." + id,
                                                     Gtk::Orientation::HORIZONTAL, 6);
        if (want_project) {
            auto* chip = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                           px + ".project." + id);
            chip->set_text(look.project);
            chip->set_ellipsize(Pango::EllipsizeMode::END);
            chip->set_max_width_chars(18);
            chip->add_css_class("jot-chip");
            chip->add_css_class("jot-project");
            line->append(*chip);
        }
        if (!meta.empty()) {
            auto* m = Gtk::make_managed<widgets::Label>(widgets::unregistered, px + ".meta." + id);
            m->set_text(meta);
            m->set_xalign(0.0f);
            m->set_hexpand(true);
            m->set_ellipsize(Pango::EllipsizeMode::END);
            m->add_css_class("dim-label");
            m->add_css_class("caption");
            line->append(*m);
        }
        body->append(*line);
    }

    // The body is the button, not the whole card: the tick has to stay
    // independently clickable, and a button wrapping a checkbox swallows it.
    auto* go = Gtk::make_managed<widgets::Button>(widgets::unregistered, px + ".goto." + id);
    go->set_has_frame(false);
    go->set_hexpand(true);
    go->add_css_class("jot-card-go");
    go->set_child(*body);
    go->signal_clicked().connect([on_open, id]() { if (on_open) on_open(id); });
    card->append(*go);
    return card;
}

// s057: the routine's dots (see TaskCard.hpp).
Gtk::Widget* routine_dots(const core::RoutineState& s, const std::string& name) {
    auto* row = Gtk::make_managed<widgets::Box>(widgets::unregistered, name + ".dots",
                                                Gtk::Orientation::HORIZONTAL, 2);
    row->add_css_class("jot-dots");
    int i = 0;
    for (const auto& o : s.recent) {
        auto* d = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                    name + ".dot." + std::to_string(i++));
        d->set_text("\u25cf");
        d->add_css_class(std::string("jot-dot-") + core::punct_word(o.punct));
        std::string tip = core::format_date(o.when).substr(0, 10);
        if (o.punct == core::Punct::OnTime) tip += " \u2014 on time";
        else if (o.punct == core::Punct::Late)
            tip += " \u2014 " + std::to_string(o.late_days) + (o.late_days == 1 ? " day late" : " days late");
        else tip += " \u2014 done";
        d->set_tooltip_text(tip);
        row->append(*d);
    }
    for (int k = 0; k < std::min(s.missed, 5); ++k) {
        auto* d = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                    name + ".miss." + std::to_string(k));
        d->set_text("\u25cb");
        d->add_css_class("jot-dot-missed");
        d->set_tooltip_text("Missed");
        row->append(*d);
    }
    return row;
}

// s057b: the note on show, marked in every list (see TaskCard.hpp).
namespace {
core::NodeId g_current;
bool names_note(const Gtk::Widget& w, const core::NodeId& id) {
    if (id.empty()) return false;
    const std::string name = w.get_name();
    const std::string tail = "." + id;
    return name.size() > tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0;
}
void walk(Gtk::Widget& w) {
    if (w.has_css_class("jot-pickable")) {
        if (names_note(w, g_current)) w.add_css_class("jot-current");
        else w.remove_css_class("jot-current");
    }
    for (auto* c = w.get_first_child(); c; c = c->get_next_sibling()) walk(*c);
}
}  // namespace

void set_current_note(const core::NodeId& id) { g_current = id; }

void pickable(Gtk::Widget& w, const core::NodeId& id) {
    w.add_css_class("jot-pickable");
    if (!id.empty() && id == g_current) w.add_css_class("jot-current");
}

void mark_current(Gtk::Widget& root) { walk(root); }

}  // namespace jot
