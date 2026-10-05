#include "TodayPane.hpp"
#include "TaskCard.hpp"
#include "core/RowLook.hpp"
#include "Log.hpp"

#include <gtkmm/cssprovider.h>
#include <gtkmm/enums.h>

#include <algorithm>
#include <ctime>

// TodayPane.cpp -- the report. Reads the index, draws rows, writes only one
// thing (a tick, straight through the NodeSource) and decides nothing.

namespace jot {
namespace {

// s039. Five view buttons at stock padding widened the whole side pane by
// ~90 px (a Paned will not go narrower than its widest page -- s037). Tighter
// padding, not shorter words: the strip's day buttons get the same.
void install_tight_css() {
    static bool done = false;
    if (done) return;
    auto display = Gdk::Display::get_default();
    if (!display) return;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(".jot-tight > button { padding-left: 4px; padding-right: 4px; min-width: 0; }");
    gtk_style_context_add_provider_for_display(
        display->gobj(), GTK_STYLE_PROVIDER(css->gobj()),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    done = true;
}

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

}  // namespace

TodayPane::TodayPane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL, 0),
      m_filter_bar("today.filters", Gtk::Orientation::HORIZONTAL, 0),
      m_b_today("today.filter_today"),
      m_b_available("today.filter_available"),
      m_b_flagged("today.filter_flagged"),
      m_b_logbook("today.filter_logbook"),
      m_b_forecast("today.filter_forecast"),
      m_day_strip("today.day_strip", Gtk::Orientation::HORIZONTAL, 0),
      m_scroll("today.scroll"),
      m_column("today.column", Gtk::Orientation::VERTICAL, 14),
      m_empty("today.empty"),
      m_head("today.head", Gtk::Orientation::VERTICAL, 0),
      m_head_title("today.head_title"),
      m_head_sub("today.head_sub"),
      m_status_fold("today.status_fold"),
      m_status_label("today.status_label"),
      m_desktop_bar("today.desktop_bar", Gtk::Orientation::VERTICAL, 2),
      m_desktop_cap("today.desktop_caption"),
      m_desktop_status("today.desktop_status"),
      m_notify_cap("today.notify_caption"),
      m_notify_status("today.notify_status"),
      m_bg_cap("today.background_caption"),
      m_bg_status("today.background_status"),
      m_prefs_button("today.preferences") {
    build_filter_bar();
    build_day_strip();

    m_column.set_margin(12);
    m_head_title.set_xalign(0.0f);
    m_head_title.add_css_class("jot-view-title");
    m_head_sub.set_xalign(0.0f);
    m_head_sub.set_wrap(true);
    m_head_sub.add_css_class("dim-label");
    m_head.append(m_head_title);
    m_head.append(m_head_sub);
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
    // s043: folded. Three paragraphs of reports took half the pane's height
    // every time Today was looked at; they are read when something is wrong.
    // So they fold, closed, and the fold's own label speaks up when one of
    // them needs reading (set_notify_status).
    m_status_label.set_text("Status");
    m_status_label.add_css_class("caption-heading");
    m_status_fold.set_label_widget(m_status_label);
    m_status_fold.set_expanded(false);
    m_status_fold.set_margin_start(12);
    m_status_fold.set_margin_end(12);
    m_status_fold.set_margin_top(4);
    m_status_fold.set_margin_bottom(4);
    m_desktop_bar.set_margin_start(0);
    m_desktop_bar.set_margin_end(0);
    m_status_fold.set_child(m_desktop_bar);
    append(m_status_fold);
}

void TodayPane::set_head(const std::string& title, const std::string& sub) {
    m_head_title.set_text(title);
    m_head_sub.set_text(sub);
    m_head_sub.set_visible(!sub.empty());
}

void TodayPane::set_desktop_status(const std::string& text) {
    m_desktop_status.set_text(text);
    m_desktop_status.set_visible(!text.empty());
}

void TodayPane::set_notify_status(const std::string& text) {
    m_notify_status.set_text(text);
    m_notify_status.set_visible(!text.empty());
    // The fold is closed, so the one report that means "you are missing
    // notifications" says so on the fold itself.
    const bool trouble = text.rfind("Not installed", 0) == 0 ||
                         text.find("no confirmation") != std::string::npos;
    m_status_label.set_text(trouble ? "Status  \u00b7  notifications need a look" : "Status");
}

void TodayPane::set_background_status(const std::string& text) {
    m_bg_status.set_text(text);
    m_bg_status.set_visible(!text.empty());
}

void TodayPane::build_filter_bar() {
    install_tight_css();
    m_filter_bar.add_css_class("linked");
    m_filter_bar.add_css_class("jot-tight");
    m_filter_bar.set_margin(8);
    m_filter_bar.set_halign(Gtk::Align::CENTER);

    // s048: icons; the view's big title below says which one you are in.
    struct { widgets::ToggleButton* b; const char* icon; View v; const char* tip; } spec[] = {
        {&m_b_today,     "jot-view-today-symbolic",     View::Today,
         "Today \u2014 available todos due by the end of today, plus everything flagged"},
        {&m_b_available, "jot-view-available-symbolic", View::Available,
         "Available \u2014 everything you could actually start right now"},
        {&m_b_flagged,   "jot-view-flagged-symbolic",   View::Flagged, "Flagged \u2014 everything you have flagged"},
        {&m_b_logbook,   "jot-view-logbook-symbolic",   View::Logbook,
         "Logbook \u2014 what got done, newest first, by day"},
        {&m_b_forecast,  "jot-view-forecast-symbolic",  View::Forecast,
         "Forecast \u2014 the days ahead: what is due, and what starts (its defer runs out), day by day"},
    };
    for (auto& s : spec) {
        s.b->set_icon_name(s.icon);
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
    m_b_logbook.set_active(v == View::Logbook);
    m_b_forecast.set_active(v == View::Forecast);
    m_day_strip.set_visible(v == View::Forecast);
    m_switching = false;
    refresh();
}

void TodayPane::set_source(core::NodeSource* src, const core::TaskIndex* tasks) {
    m_src   = src;
    m_tasks = tasks;
    refresh();
}

// One row: since s042, THE card (TaskCard) -- the same builder Tags uses, so a
// todo looks the same wherever it is listed. The tick writes straight through
// the NodeSource; the row leaves when the MODEL says so, never by itself.
// `show_project` is off under a heading that already names the project.
Gtk::Widget* TodayPane::task_row(const core::Node& n, bool show_project) {
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    CardOpts o;
    o.prefix       = "today";
    o.show_project = show_project;
    return task_card(*m_src, n, now, o, [this](const core::NodeId& id) { m_sig_goto.emit(id); });
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
        if (const core::Node* n = m_src->find(id)) group->append(*task_row(*n, p == nullptr));

    m_column.append(*group);
}

void TodayPane::refresh() {
    while (auto* c = m_column.get_first_child()) m_column.remove(*c);
    m_column.append(m_head);
    m_column.append(m_empty);
    m_empty.set_visible(false);

    if (!m_src || !m_tasks) { set_head("Today", {}); m_empty.set_visible(true); return; }
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    if (m_view == View::Logbook) {   // looks back: no Overdue pin, nothing to add up
        set_head("Logbook", "What got done, newest first");
        fill_logbook(now);
        return;
    }
    if (m_view == View::Forecast) { fill_forecast(now); return; }  // s039: pins Overdue itself

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

    std::vector<core::NodeId> all = overdue;
    all.insert(all.end(), rest.begin(), rest.end());
    set_head(m_view == View::Available ? "Available"
             : m_view == View::Flagged ? "Flagged" : "Today",
             core::summary_text(core::summarize(*m_src, all, now)));

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
            case View::Logbook: break;   // fill_logbook says its own
            case View::Forecast: break;  // fill_forecast says its own
        }
        m_empty.set_visible(true);
    }

    if (auto lg = log::get(log::Area::Drawer))
        lg->debug("today: view={} overdue={} rows={} of {} todo(s)",
                  static_cast<int>(m_view), overdue.size(), rest.size(),
                  m_tasks->count());
}

// ─────────────────────────────────────────────────────────────────────────────
// The Logbook (s032). Same shape as the forward views -- a heading per group,
// rows under it -- but the group is a DAY and the order is newest first.
//
// A todo's row keeps its tick, ticked: unticking it is how you say "that was
// not done after all", and the row leaves because the model said so, exactly
// as a tick in Today does. A project that is a plain note has no tick to
// offer; its row says Completed or Dropped instead.
//
// Capped, because every row is a widget and a year of ticks is thousands. The
// cap says so on screen rather than quietly ending the list.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
constexpr std::size_t kLogbookCap = 300;
}

Gtk::Widget* TodayPane::log_row(const core::LogEntry& e) {
    // A repeat record's note may have been renamed or deleted since; the
    // record carries its own title, and `n` is only where the row LEADS.
    const core::Node* np = m_src->find(e.id);
    core::Node blank;
    blank.id = e.id;
    const core::Node& n = np ? *np : blank;
    const std::string key = e.repeat ? e.id + "." + std::to_string(e.when) : e.id;
    auto* row = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                "today.logrow." + key,
                                                Gtk::Orientation::HORIZONTAL, 8);
    const core::NodeId id = n.id;
    if (!e.repeat && n.task.is_task && n.task.done) {
        auto* tick = Gtk::make_managed<widgets::CheckButton>(widgets::unregistered,
                                                             "today.logtick." + key);
        tick->set_valign(Gtk::Align::CENTER);
        tick->set_active(true);
        tick->set_tooltip_text("Untick: not done after all");
        tick->signal_toggled().connect([this, id, tick]() {
            if (m_src) m_src->set_done(id, tick->get_active());
        });
        row->append(*tick);
    } else {
        // No tick to offer (a note project): hold its place so the titles line up.
        auto* gap = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                    "today.loggap." + key,
                                                    Gtk::Orientation::HORIZONTAL, 0);
        gap->set_size_request(16, -1);
        row->append(*gap);
    }

    auto* text = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                 "today.logtext." + key,
                                                 Gtk::Orientation::VERTICAL, 0);
    auto* title = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                    "today.logtitle." + key);
    const std::string& name = e.repeat ? e.title : n.title;
    title->set_text(name.empty() ? "(untitled)" : name);
    title->set_xalign(0.0f);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    if (!name.empty()) title->set_tooltip_text(name);
    if (e.kind == core::ProjectState::Dropped) title->add_css_class("dim-label");
    text->append(*title);

    // "14:32 · Completed project · in Home" -- the time, what kind of finish
    // when it is not a plain tick, and where it lives.
    std::string sub = core::format_clock(e.when);
    auto add = [&sub](const std::string& part) {
        if (part.empty()) return;
        sub += sub.empty() ? part : "  ·  " + part;
    };
    if (e.repeat)                               add("\u21bb Repeats");
    else if (e.kind == core::ProjectState::Dropped) add("Dropped");
    else if (!(n.task.is_task && n.task.done)) add("Completed");
    if (const core::Node* p = n.parent_id.empty() ? nullptr : m_src->find(n.parent_id))
        add("in " + titled(p));
    if (!sub.empty()) {
        auto* when = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                       "today.logwhen." + key);
        when->set_text(sub);
        when->set_xalign(0.0f);
        when->set_ellipsize(Pango::EllipsizeMode::END);
        when->add_css_class("dim-label");
        when->add_css_class("caption");
        text->append(*when);
    }

    auto* go = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                  "today.loggoto." + key);
    go->set_has_frame(false);
    go->set_hexpand(true);
    go->set_child(*text);
    const bool exists = np != nullptr;
    go->signal_clicked().connect([this, id, exists]() { if (exists) m_sig_goto.emit(id); });
    row->append(*go);
    return row;
}

void TodayPane::fill_logbook(std::int64_t now) {
    const auto all = core::logbook(*m_src);
    std::vector<core::LogEntry> shown(all.begin(),
                                      all.begin() + std::min(all.size(), kLogbookCap));
    for (const auto& day : core::group_by_day(shown)) {
        const std::string key = std::to_string(day.day);
        auto* group = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                      "today.logday." + key,
                                                      Gtk::Orientation::VERTICAL, 4);
        auto* head = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                       "today.logday_head." + key);
        head->set_text(core::day_label(day.day, now) + "  ·  " +
                       std::to_string(day.entries.size()));
        head->set_xalign(0.0f);
        head->add_css_class("heading");
        group->append(*head);
        if (day.day == 0) {
            auto* why = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                          "today.logday_why");
            why->set_text("Finished before jot kept the time.");
            why->set_xalign(0.0f);
            why->add_css_class("dim-label");
            why->add_css_class("caption");
            group->append(*why);
        }
        for (const auto& e : day.entries)
            if (e.repeat || m_src->find(e.id)) group->append(*log_row(e));
        m_column.append(*group);
    }

    if (all.size() > shown.size()) {
        auto* more = Gtk::make_managed<widgets::Label>(widgets::unregistered, "today.logmore");
        more->set_text("…and " + std::to_string(all.size() - shown.size()) +
                       " older, not shown.");
        more->set_xalign(0.0f);
        more->add_css_class("dim-label");
        m_column.append(*more);
    }

    if (all.empty()) {
        m_empty.set_text("Nothing done yet.\n\nTick a todo, or mark a project Completed or "
                         "Dropped, and it is listed here under the day it happened.");
        m_empty.set_visible(true);
    }

    if (auto lg = log::get(log::Area::Drawer))
        lg->debug("today: logbook {} entr(ies), {} shown", all.size(), shown.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// The Forecast (s039). core::forecast decides what lands on which day; this
// draws a strip of days with a count on each, and under it the picked day:
// what is DUE, then what STARTS (its defer runs out). Later is every day after
// the strip that has something on it. Overdue stays pinned on top, as in every
// forward view.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

std::string strf(std::int64_t when, const char* fmt) {
    std::time_t t = static_cast<std::time_t>(when);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}

std::string day_head(std::int64_t day, std::int64_t today) {
    if (day == today) return "Today  ·  " + strf(day, "%a %e %b");
    if (day == core::next_day(today)) return "Tomorrow  ·  " + strf(day, "%a %e %b");
    return strf(day, "%A %e %B");
}

}  // namespace

void TodayPane::build_day_strip() {
    m_day_strip.add_css_class("linked");
    m_day_strip.add_css_class("jot-tight");
    m_day_strip.set_margin_start(8);
    m_day_strip.set_margin_end(8);
    m_day_strip.set_margin_bottom(4);
    for (int i = 0; i <= kStripDays; ++i) {
        const std::string k = std::to_string(i);
        auto* b = Gtk::make_managed<widgets::ToggleButton>(widgets::unregistered, "today.day." + k);
        auto* box = Gtk::make_managed<widgets::Box>(widgets::unregistered, "today.daybox." + k,
                                                    Gtk::Orientation::VERTICAL, 0);
        auto* name = Gtk::make_managed<widgets::Label>(widgets::unregistered, "today.dayname." + k);
        auto* num = Gtk::make_managed<widgets::Label>(widgets::unregistered, "today.daynum." + k);
        auto* cnt = Gtk::make_managed<widgets::Label>(widgets::unregistered, "today.daycount." + k);
        name->add_css_class("caption");
        num->add_css_class("heading");
        cnt->add_css_class("caption");
        cnt->add_css_class("dim-label");
        box->append(*name);
        box->append(*num);
        box->append(*cnt);
        b->set_child(*box);
        b->set_hexpand(true);
        const int pick = i;
        b->signal_toggled().connect([this, pick, b]() {
            if (m_switching) return;
            if (!b->get_active()) { b->set_active(true); return; }   // a radio has no "off"
            m_day_pick = pick;
            refresh();
        });
        m_day_btn[i] = b;
        m_day_name[i] = name;
        m_day_num[i] = num;
        m_day_count[i] = cnt;
        m_day_strip.append(*b);
    }
    m_day_strip.set_visible(false);
    append(m_day_strip);
}

void TodayPane::add_day_rows(const std::string& key, const std::string& head,
                             const core::ForecastDay& d) {
    auto* group = Gtk::make_managed<widgets::Box>(widgets::unregistered, "today.fday." + key,
                                                  Gtk::Orientation::VERTICAL, 4);
    auto* h = Gtk::make_managed<widgets::Label>(widgets::unregistered, "today.fday_head." + key);
    h->set_text(head + "  ·  " + std::to_string(d.count()));
    h->set_xalign(0.0f);
    h->add_css_class("heading");
    group->append(*h);
    auto part = [&](const char* what, const std::vector<core::NodeId>& ids, const char* tag) {
        if (ids.empty()) return;
        auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                    std::string("today.fday_") + tag + "." + key);
        l->set_text(what);
        l->set_xalign(0.0f);
        l->add_css_class("dim-label");
        l->add_css_class("caption");
        group->append(*l);
        for (const auto& id : ids)
            if (const core::Node* n = m_src->find(id)) group->append(*task_row(*n, true));
    };
    part("Due", d.due, "due");
    std::vector<core::NodeId> starts;   // a todo due AND starting today is listed once, under Due
    for (const auto& id : d.starts)
        if (std::find(d.due.begin(), d.due.end(), id) == d.due.end()) starts.push_back(id);
    part("Starts \u2014 its defer runs out", starts, "starts");
    m_column.append(*group);
}

void TodayPane::fill_forecast(std::int64_t now) {
    const core::Forecast f = core::forecast(*m_src, *m_tasks, now, kStripDays);
    const std::int64_t today = core::day_start(now);

    // The strip: relabelled, never rebuilt.
    std::size_t later_n = 0;
    for (const auto& d : f.later) later_n += d.count();
    m_switching = true;
    for (int i = 0; i <= kStripDays; ++i) {
        const bool is_later = i == kStripDays;
        const std::size_t n = is_later ? later_n : f.days[i].count();
        // Weekday names only -- "Today" in all eight would set the strip's
        // width (and so the pane's) by the longest word. Today is the first
        // cell, and its tooltip and heading say so.
        m_day_name[i]->set_text(is_later ? "Later" : strf(f.days[i].day, "%a"));
        m_day_num[i]->set_text(is_later ? "»" : strf(f.days[i].day, "%e"));
        m_day_count[i]->set_text(n ? std::to_string(n) : "·");
        m_day_btn[i]->set_tooltip_text(is_later ? std::string("Everything after the strip")
                                                : day_head(f.days[i].day, today));
        m_day_btn[i]->set_active(i == m_day_pick);
    }
    m_switching = false;

    const auto overdue = m_tasks->query(*m_src, core::Filter::Overdue, now);
    if (!overdue.empty())
        add_group("", overdue, "Overdue  ·  " + std::to_string(overdue.size()));

    {
        // s043: the head adds up what is on screen -- the pinned Overdue and
        // the picked day (or Later).
        std::vector<core::NodeId> shown = overdue;
        const auto take = [&](const core::ForecastDay& d) {
            shown.insert(shown.end(), d.due.begin(), d.due.end());
            for (const auto& id : d.starts)
                if (std::find(d.due.begin(), d.due.end(), id) == d.due.end()) shown.push_back(id);
        };
        if (m_day_pick < kStripDays) take(f.days[m_day_pick]);
        else for (const auto& d : f.later) take(d);
        set_head("Forecast", core::summary_text(core::summarize(*m_src, shown, now)));
    }

    bool any = false;
    if (m_day_pick < kStripDays) {
        const auto& d = f.days[m_day_pick];
        if (d.count()) {
            add_day_rows(std::to_string(d.day), day_head(d.day, today), d);
            any = true;
        }
        if (!any) {
            m_empty.set_text(day_head(d.day, today) + "\n\nNothing due and nothing starting. "
                             "A todo lands here by its due date, or by its defer date "
                             "running out.");
        }
    } else {
        for (const auto& d : f.later) add_day_rows(std::to_string(d.day), day_head(d.day, today), d);
        any = !f.later.empty();
        if (!any) m_empty.set_text("Nothing due or starting after the next seven days.");
    }
    if (!any) m_empty.set_visible(true);

    if (auto lg = log::get(log::Area::Drawer))
        lg->debug("today: forecast pick={} overdue={} later_days={}", m_day_pick, overdue.size(),
                  f.later.size());
}

}  // namespace jot
