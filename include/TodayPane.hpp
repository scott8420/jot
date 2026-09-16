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

    // ── the desktop footer (s009) ──────────────────────────────────────────
    // Today is the report of what is on today; the GNOME calendar card is the
    // same question asked somewhere else. So the switch for the projection
    // lives HERE, under the report it mirrors, rather than two levels into a
    // menu where a feature you cannot see is a feature you do not know you have.
    //
    // THE PANE DOES NOT KNOW EVOLUTION DATA SERVER EXISTS. It emits a request
    // and it renders a string; the Shell owns the backend and decides what the
    // string says. Same contract as signal_goto: this pane asks, it never
    // decides.
    void set_desktop_available(bool can);        // false => the box is insensitive
    void set_desktop_on(bool on);                // set WITHOUT re-emitting
    void set_desktop_status(const std::string& text);

    // ── the notification half (s011) ───────────────────────────────────────
    // Second box in the same footer, and they sit together because they are two
    // answers to one question: how does the desktop tell me a deadline arrived.
    // The calendar row is what it knows while jot is CLOSED; the notification is
    // what it says while jot is open, and it is the one you can click.
    void set_notify_on(bool on);                 // set WITHOUT re-emitting
    void set_notify_status(const std::string& text);

    // ── the residency half (s012) ──────────────────────────────────────────
    // Third box, same footer, and it belongs with the other two rather than in
    // a preferences window jot does not have: it is the PRECONDITION for both.
    // A calendar row is written while jot runs and a notification is sent while
    // jot runs, so "does jot keep running" is the first of the three questions,
    // not a separate subject. It sits under them because it is the one you
    // reach for once the other two have disappointed you.
    void set_background_on(bool on);             // set WITHOUT re-emitting
    void set_background_status(const std::string& text);

    sigc::signal<void(bool)>& signal_desktop_toggled() { return m_sig_desktop; }
    sigc::signal<void(bool)>& signal_notify_toggled()  { return m_sig_notify; }
    sigc::signal<void(bool)>& signal_background_toggled() { return m_sig_background; }

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

    // The footer. A CheckButton rather than a Gtk::Switch because the rest of
    // jot has no switches and one would read as a different kind of control.
    widgets::Box        m_desktop_bar;
    widgets::CheckButton m_desktop_check;
    widgets::Label       m_desktop_status;
    bool                 m_setting_desktop = false;   // the m_switching guard, again
    widgets::CheckButton m_notify_check;
    widgets::Label       m_notify_status;
    bool                 m_setting_notify  = false;
    widgets::CheckButton m_bg_check;
    widgets::Label       m_bg_status;
    bool                 m_setting_bg      = false;

    // True while the filter buttons are being set programmatically: they are a
    // radio group built out of ToggleButtons, so setting one clears another and
    // both emit.
    bool m_switching = false;

    sigc::signal<void(core::NodeId)> m_sig_goto;
    sigc::signal<void(bool)>         m_sig_desktop;
    sigc::signal<void(bool)>         m_sig_notify;
    sigc::signal<void(bool)>         m_sig_background;
};

}  // namespace jot
