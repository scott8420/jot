#pragma once
#include "core/Nodes.hpp"
#include "core/Tasks.hpp"
#include "core/Forecast.hpp"
#include "widgets/Widgets.hpp"

#include <sigc++/signal.h>

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// TodayPane -- the report, and the surface s007 ends at.
//
// It shares the left pane with the tree: the pane stops being "the tree" and
// becomes HOW YOU NAVIGATE, with two ways of looking at the same nodes. Three
// panes still hold, and nothing moved.
//
// IT GROUPS BY PARENT, and that is the whole point rather than a layout choice.
// Scott asked for a report of what is on the list and how / where / why / when
// to do it, and every part of that already has a home in the model:
//
//     why    the PARENT's note -- the project's own prose
//     how    the todo's own body, in markdown
//     where  its tags
//     when   due / defer / availability
//     order  sibling order + flagged
//
// So a group header is a parent title with the first line of the parent's note
// under it, and the rows beneath it are its available children. An OmniFocus
// task's note is a plain-text scratch field nobody fills in; a jot task IS a
// note and so is its parent, which is why jot can write this report and
// OmniFocus cannot.
//
// IT COMPUTES NOTHING ITSELF. Every row it draws came out of core::TaskIndex
// and core::availability -- the pane asks, it does not decide. A view that
// decided what was available would be a second answer to the question core
// already answers under test, and the two would drift.
//
// It reads a NodeSource and a const TaskIndex& and owns neither. Clicking a row
// emits signal_goto and the Shell decides what that means, exactly as the
// drawer does: this pane does not know the tree exists.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class TodayPane : public widgets::Box {
public:
    explicit TodayPane(std::string_view name);

    void set_source(core::NodeSource* src, const core::TaskIndex* tasks);
    void refresh();

    // ── the footer: three REPORTS (s009, s011, s012; reshaped s016a) ───────
    // Each of these features happens in another process -- the calendar
    // drop-down, the notification tray, a jot with no window -- so the sentence
    // saying what happened, and when, is the only instrument. It stays here,
    // under the report it belongs beside.
    //
    // THE SWITCHES DO NOT. They lived here until s016a, when they moved to the
    // Preferences window alone (Scott's call: a pane about today's tasks is not
    // a settings page). The footer ends in a "Preferences..." button instead.
    //
    // THE PANE DOES NOT KNOW EVOLUTION DATA SERVER EXISTS, nor the notification
    // daemon. It renders strings; the Shell decides what they say.
    void set_desktop_status(const std::string& text);
    void set_notify_status(const std::string& text);
    void set_background_status(const std::string& text);

    // A row was clicked: show me that note. Same contract as the drawer's.
    sigc::signal<void(core::NodeId)>& signal_goto() { return m_sig_goto; }

private:
    // The three views, and deliberately only three. Forecast and a review queue
    // are perspectives over the same index and they are worthless without a
    // real corpus to look at, so they are not built until there is one.
    // s032: Logbook -- what got done, newest first, by day. It looks BACK,
    // where the other three look forward; it is the fourth button rather than
    // a fifth tab because it is still a report over the same nodes.
    // s039: Forecast -- the days ahead, due and starting. A fifth button, not
    // a sixth tab, for the Logbook's reason: a report over the same todos.
    enum class View { Today, Available, Flagged, Logbook, Forecast };

    void build_filter_bar();
    void build_desktop_bar();
    void add_group(const core::NodeId& parent, const std::vector<core::NodeId>& ids,
                   const std::string& override_head);
    Gtk::Widget* task_row(const core::Node& n, bool show_project);
    // s055: one deadline running short -- its head, its runway, its next step.
    void add_pull_group(const core::NodeId& deadline, const core::NodeId& step, std::int64_t now);
    Gtk::Widget* log_row(const core::LogEntry& e);
    void         fill_logbook(std::int64_t now);
    void         build_day_strip();                 // s039
    void         fill_forecast(std::int64_t now);   // s039
    void         add_day_rows(const std::string& key, const std::string& head,
                              const core::ForecastDay& d);
    void set_view(View v);

    core::NodeSource*      m_src   = nullptr;   // not owned; the seam
    const core::TaskIndex* m_tasks = nullptr;   // not owned; the Shell keeps it current
    View                   m_view  = View::Today;

    widgets::Box            m_filter_bar;
    widgets::ToggleButton   m_b_today, m_b_available, m_b_flagged, m_b_logbook, m_b_forecast;
    // s039: the Forecast's strip -- today, the six days after it, and Later.
    // Built once and relabelled on each refresh, so a click never destroys the
    // button it came from. Shown only in the Forecast view.
    static constexpr int kStripDays = 7;
    widgets::Box            m_day_strip;
    widgets::ToggleButton*  m_day_btn[kStripDays + 1] = {};
    widgets::Label*         m_day_name[kStripDays + 1] = {};
    widgets::Label*         m_day_num[kStripDays + 1] = {};
    widgets::Label*         m_day_count[kStripDays + 1] = {};
    int                     m_day_pick = 0;   // 0 = today .. kStripDays = Later
    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_column;
    widgets::Label          m_empty;
    // s043: the view's head -- a big title and what the list adds up to.
    // Members (not rebuilt), re-appended at the top of the column each refresh.
    widgets::Box            m_head;
    widgets::Label          m_head_title;
    widgets::Label          m_head_sub;
    void set_head(const std::string& title, const std::string& sub);
    // s043: the footer's reports, folded -- they are read when something
    // goes wrong, not every time the list is.
    widgets::Expander       m_status_fold;
    widgets::Label          m_status_label;

    // The footer: a caption and a status sentence per feature, and the way
    // to Preferences. No check boxes -- see the public block above.
    widgets::Box    m_desktop_bar;
    widgets::Label  m_desktop_cap;
    widgets::Label  m_desktop_status;
    widgets::Label  m_notify_cap;
    widgets::Label  m_notify_status;
    widgets::Label  m_bg_cap;
    widgets::Label  m_bg_status;
    widgets::Button m_prefs_button;

    // True while the filter buttons are being set programmatically: they are a
    // radio group built out of ToggleButtons, so setting one clears another and
    // both emit.
    bool m_switching = false;

    sigc::signal<void(core::NodeId)> m_sig_goto;
};

}  // namespace jot
