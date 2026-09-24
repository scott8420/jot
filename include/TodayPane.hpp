#pragma once
#include "core/Nodes.hpp"
#include "core/Tasks.hpp"
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
    enum class View { Today, Available, Flagged };

    void build_filter_bar();
    void build_desktop_bar();
    void add_group(const core::NodeId& parent, const std::vector<core::NodeId>& ids,
                   const std::string& override_head);
    Gtk::Widget* task_row(const core::Node& n);
    void set_view(View v);

    core::NodeSource*      m_src   = nullptr;   // not owned; the seam
    const core::TaskIndex* m_tasks = nullptr;   // not owned; the Shell keeps it current
    View                   m_view  = View::Today;

    widgets::Box            m_filter_bar;
    widgets::ToggleButton   m_b_today, m_b_available, m_b_flagged;
    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_column;
    widgets::Label          m_empty;

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
