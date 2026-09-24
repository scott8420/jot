#include "TodayPane.hpp"
#include "Log.hpp"

#include <gtkmm/enums.h>

#include <ctime>

// TodayPane.cpp -- the report. Reads the index, draws rows, writes only one
// thing (a tick, straight through the NodeSource) and decides nothing.

namespace jot {
namespace {

std::string titled(const core::Node* n) {
    if (!n) return {};
    return n->title.empty() ? std::string("Untitled") : n->title;
}

// The WHY, in one line: the first non-blank, non-heading line of the parent's
// note. Headings are skipped because a project note very often opens with its
// own title as an H1, and repeating the group header under the group header
// says nothing.
std::string why_line(const core::Node* parent) {
    if (!parent) return {};
    const std::string& b = parent->body;
    std::size_t i = 0;
    while (i < b.size()) {
        std::size_t e = b.find('\n', i);
        if (e == std::string::npos) e = b.size();
        std::string line = b.substr(i, e - i);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const bool blank   = line.find_first_not_of(" \t") == std::string::npos;
        const bool heading = !line.empty() && line.front() == '#';
        if (!blank && !heading) {
            if (line.size() > 90) line = line.substr(0, 88) + "\u2026";
            return line;
        }
        i = e + 1;
    }
    return {};
}

// What a row says about its own timing, after the row's title. Short on
// purpose: the pane is narrow, and the long answer is in the drawer.
std::string when_line(const core::NodeSource& src, const core::Node& n,
                      std::int64_t now) {
    const std::int64_t due = core::effective_due(src, n.id);
    std::string out;
    if (due != 0) {
        out = (due < now ? "Overdue \u2014 " : "Due ") + core::format_date(due);
        if (due != n.task.due) out += " (from a parent)";
    }
    if (n.task.flagged) out += out.empty() ? "Flagged" : "  \u00b7  Flagged";
    return out;
}

}  // namespace

TodayPane::TodayPane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL, 0),
      m_filter_bar("today.filters", Gtk::Orientation::HORIZONTAL, 0),
      m_b_today("today.filter_today"),
      m_b_available("today.filter_available"),
      m_b_flagged("today.filter_flagged"),
      m_scroll("today.scroll"),
      m_column("today.column", Gtk::Orientation::VERTICAL, 14),
      m_empty("today.empty"),
      m_desktop_bar("today.desktop_bar", Gtk::Orientation::VERTICAL, 2),
      m_desktop_cap("today.desktop_caption"),
      m_desktop_status("today.desktop_status"),
      m_notify_cap("today.notify_caption"),
      m_notify_status("today.notify_status"),
      m_bg_cap("today.background_caption"),
      m_bg_status("today.background_status"),
      m_prefs_button("today.preferences") {
    build_filter_bar();

    m_column.set_margin(12);
    m_empty.set_wrap(true);
    m_empty.set_xalign(0.0f);
    m_empty.add_css_class("dim-label");
    m_column.append(m_empty);

    m_scroll.set_child(m_column);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    append(m_scroll);

    build_desktop_bar();
}

// ─────────────────────────────────────────────────────────────────────────────
// build_desktop_bar -- the footer: REPORTS, not settings (s016a).
//
// s009 put the projection's check box here, s011 and s012 added two more, and
// the footer became the app's settings page by accretion -- a pane about
// today's tasks carrying three switches about how jot behaves. s014 built a
// real Preferences window and left the boxes in both places; s016a (Scott's
// call) takes them out of here.
//
// What STAYS is the half that could never have moved: the status lines. Each of
// these features lands in ANOTHER PROCESS -- a calendar drop-down, the
// notification tray, a process with no window -- so the sentence that says what
// happened and when is the only instrument there is, and it has to be visible
// where the user already looks, not two clicks into a dialog. A setting is
// changed once; a report is read every day.
//
// Each line gets a caption now that the check box that used to title it is
// gone, and one flat "Preferences..." button at the bottom goes to where the
// switches live.
// ─────────────────────────────────────────────────────────────────────────────
void TodayPane::build_desktop_bar() {
    m_desktop_bar.set_margin_start(12);
    m_desktop_bar.set_margin_end(12);
    m_desktop_bar.set_margin_top(6);
    m_desktop_bar.set_margin_bottom(6);
    m_desktop_bar.set_spacing(2);

    // One report: a caption and the sentence under it. The caption is what the
    // check box's label used to be, shortened to a noun -- it names the feature,
    // it no longer offers to change it.
    auto report = [this](widgets::Label& cap, widgets::Label& status,
                         const char* caption) {
        cap.set_text(caption);
        cap.set_xalign(0.0f);
        cap.add_css_class("caption-heading");
        cap.set_margin_top(4);
        status.set_xalign(0.0f);
        status.set_wrap(true);
        status.add_css_class("dim-label");
        status.add_css_class("caption");
        m_desktop_bar.append(cap);
        m_desktop_bar.append(status);
    };
    report(m_desktop_cap, m_desktop_status, "Desktop calendar");
    report(m_notify_cap,  m_notify_status,  "Due notifications");
    report(m_bg_cap,      m_bg_status,      "When the window closes");

    m_prefs_button.set_label("Preferences\u2026");
    m_prefs_button.set_has_frame(false);
    m_prefs_button.set_halign(Gtk::Align::START);
    m_prefs_button.set_margin_top(4);
    m_prefs_button.add_css_class("caption");
    m_prefs_button.set_tooltip_text("Turn these on or off");
    // win.preferences through the muxer, like every other button in jot that
    // opens something the Shell owns. This pane does not know the window exists.
    m_prefs_button.set_action_name("win.preferences");
    m_desktop_bar.append(m_prefs_button);

    auto* rule = Gtk::make_managed<widgets::Separator>(
        "today.desktop_rule", Gtk::Orientation::HORIZONTAL);
    append(*rule);
    append(m_desktop_bar);
}

void TodayPane::set_desktop_status(const std::string& text) {
    m_desktop_status.set_text(text);
    m_desktop_status.set_visible(!text.empty());
}

void TodayPane::set_notify_status(const std::string& text) {
    m_notify_status.set_text(text);
    m_notify_status.set_visible(!text.empty());
}

void TodayPane::set_background_status(const std::string& text) {
    m_bg_status.set_text(text);
    m_bg_status.set_visible(!text.empty());
}

void TodayPane::build_filter_bar() {
    m_filter_bar.add_css_class("linked");
    m_filter_bar.set_margin(8);
    m_filter_bar.set_halign(Gtk::Align::CENTER);

    struct { widgets::ToggleButton* b; const char* label; View v; const char* tip; } spec[] = {
        {&m_b_today,     "Today",     View::Today,
         "Available todos due by the end of today, plus everything flagged"},
        {&m_b_available, "Available", View::Available,
         "Everything you could actually start right now"},
        {&m_b_flagged,   "Flagged",   View::Flagged, "Everything you have flagged"},
    };
    for (auto& s : spec) {
        s.b->set_label(s.label);
        s.b->set_tooltip_text(s.tip);
        s.b->set_hexpand(true);
        const View v = s.v;
        s.b->signal_toggled().connect([this, v, b = s.b]() {
            if (m_switching) return;
            if (!b->get_active()) { b->set_active(true); return; }   // a radio has no "off"
            set_view(v);
        });
        m_filter_bar.append(*s.b);
    }
    m_switching = true;
    m_b_today.set_active(true);
    m_switching = false;
    append(m_filter_bar);
}

void TodayPane::set_view(View v) {
    m_view = v;
    m_switching = true;
    m_b_today.set_active(v == View::Today);
    m_b_available.set_active(v == View::Available);
    m_b_flagged.set_active(v == View::Flagged);
    m_switching = false;
    refresh();
}

void TodayPane::set_source(core::NodeSource* src, const core::TaskIndex* tasks) {
    m_src   = src;
    m_tasks = tasks;
    refresh();
}

// One row: a tick box, the title, and the timing under it. The tick writes
// straight through the NodeSource -- the pane does not remove the row itself,
// it waits to be told, exactly as the tree waits for a drop to be announced.
// The row you just ticked disappearing because the MODEL said so is the whole
// difference between a view and a second copy of the truth.
Gtk::Widget* TodayPane::task_row(const core::Node& n) {
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

    auto* row = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                "today.row." + n.id,
                                                Gtk::Orientation::HORIZONTAL, 8);
    auto* tick = Gtk::make_managed<widgets::CheckButton>(widgets::unregistered,
                                                         "today.tick." + n.id);
    tick->set_valign(Gtk::Align::CENTER);
    tick->set_active(n.task.done);          // BEFORE the handler: set_active emits
    const core::NodeId id = n.id;
    tick->signal_toggled().connect([this, id, tick]() {
        if (!m_src) return;
        m_src->set_done(id, tick->get_active());
    });
    row->append(*tick);

    auto* text = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                 "today.rowtext." + n.id,
                                                 Gtk::Orientation::VERTICAL, 0);
    auto* title = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                    "today.title." + n.id);
    title->set_text(n.title.empty() ? "(untitled)" : n.title);
    title->set_xalign(0.0f);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    if (!n.title.empty()) title->set_tooltip_text(n.title);
    text->append(*title);

    const std::string when = when_line(*m_src, n, now);
    if (!when.empty()) {
        auto* sub = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                      "today.when." + n.id);
        sub->set_text(when);
        sub->set_xalign(0.0f);
        sub->add_css_class("dim-label");
        sub->add_css_class("caption");
        if (core::effective_due(*m_src, n.id) != 0 &&
            core::effective_due(*m_src, n.id) < now)
            sub->add_css_class("jot-overdue");   // jot's own, so dark mode gets a pink that reads as late rather than as alarm (Appearance.cpp)
        text->append(*sub);
    }

    // The title is the button, not the whole row: the tick box has to stay
    // independently clickable, and a button wrapping a checkbox swallows it.
    auto* go = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                  "today.goto." + n.id);
    go->set_has_frame(false);
    go->set_hexpand(true);
    go->set_child(*text);
    go->signal_clicked().connect([this, id]() { m_sig_goto.emit(id); });
    row->append(*go);
    return row;
}

void TodayPane::add_group(const core::NodeId& parent,
                          const std::vector<core::NodeId>& ids,
                          const std::string& override_head) {
    auto* group = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                  "today.group." + parent,
                                                  Gtk::Orientation::VERTICAL, 4);
    const core::Node* p = parent.empty() ? nullptr : m_src->find(parent);

    auto* head = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                   "today.group_head." + parent);
    // A top-level todo has no parent and therefore no project. It is NOT put in
    // an Inbox: unfiled is a state, not a place, and there is no second list
    // here to empty or feel guilty about.
    head->set_text(!override_head.empty() ? override_head
                   : (p ? titled(p) + "  \u00b7  " + std::to_string(ids.size())
                        : "Unfiled"));
    head->set_xalign(0.0f);
    head->add_css_class("heading");
    group->append(*head);

    // The WHY. This line is the reason the grouping is by parent at all.
    const std::string why = why_line(p);
    if (!why.empty()) {
        auto* sub = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                      "today.why." + parent);
        sub->set_text(why);
        sub->set_xalign(0.0f);
        sub->set_wrap(true);
        sub->add_css_class("dim-label");
        sub->add_css_class("caption");
        group->append(*sub);
    }

    for (const auto& id : ids)
        if (const core::Node* n = m_src->find(id)) group->append(*task_row(*n));

    m_column.append(*group);
}

void TodayPane::refresh() {
    while (auto* c = m_column.get_first_child()) m_column.remove(*c);
    m_column.append(m_empty);
    m_empty.set_visible(false);

    if (!m_src || !m_tasks) { m_empty.set_visible(true); return; }
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

    core::Filter f = core::Filter::Today;
    if (m_view == View::Available) f = core::Filter::Available;
    if (m_view == View::Flagged)   f = core::Filter::Flagged;

    // Overdue is pinned above whatever view you are in, ungrouped and never
    // filtered out by it. A late task is the one piece of news that should not
    // depend on which tab you happen to be looking at -- and unlike everything
    // below, it is shown even when a sequence blocks it, because "you cannot
    // start this yet and it was due on Tuesday" is exactly the sentence you
    // need to have read.
    const auto overdue = m_tasks->query(*m_src, core::Filter::Overdue, now);
    if (!overdue.empty()) add_group("", overdue, "Overdue  \u00b7  " +
                                    std::to_string(overdue.size()));

    const auto rows = m_tasks->query(*m_src, f, now);
    std::vector<core::NodeId> rest;
    for (const auto& id : rows)
        if (std::find(overdue.begin(), overdue.end(), id) == overdue.end())
            rest.push_back(id);

    for (const auto& g : core::group_by_parent(*m_src, rest))
        add_group(g.parent, g.tasks, {});

    if (overdue.empty() && rest.empty()) {
        // An empty Today is a REPORT, not a failure, and it must not read like
        // one. The three views have different empty meanings and each says its
        // own, because "Nothing here" on a list you know has todos in it looks
        // like a bug.
        switch (m_view) {
            case View::Today:
                m_empty.set_text("Nothing due today and nothing flagged.\n\n"
                                 "Todos appear here when they are available \u2014 not "
                                 "done, not deferred, and not waiting behind an "
                                 "earlier step in a sequence.");
                break;
            case View::Available:
                m_empty.set_text("Nothing is available right now.\n\n"
                                 "Everything is either done, deferred to a later "
                                 "date, or blocked behind an earlier step.");
                break;
            case View::Flagged:
                m_empty.set_text("Nothing is flagged.\n\nFlag a todo in the details "
                                 "pane when it is the one you mean to do next.");
                break;
        }
        m_empty.set_visible(true);
    }

    if (auto lg = log::get(log::Area::Drawer))
        lg->debug("today: view={} overdue={} rows={} of {} todo(s)",
                  static_cast<int>(m_view), overdue.size(), rest.size(),
                  m_tasks->count());
}

}  // namespace jot
