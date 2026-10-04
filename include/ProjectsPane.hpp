#pragma once
#include "core/Nodes.hpp"
#include "widgets/Widgets.hpp"

#include <sigc++/signal.h>

#include <cstdint>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// ProjectsPane -- Review and the Projects list (s037, road item 8).
//
// The fifth view in the left pane: Notes | Inbox | Today | Tags | Projects.
// One concept (your projects), one widget: two joined buttons at the top pick
// how you look at them --
//
//   Review   the projects due their look, the longest-waiting first. Each row
//            has a Reviewed button; pressing it starts that project's clock
//            again and the row leaves. Empty means you are done for now, and
//            it says when the next one comes up.
//   All      every project by where it stands: Active, On hold, then
//            Completed and Dropped folded at the foot (s031 deferred this
//            list to here).
//
// IT DECIDES NOTHING. Which projects, in what order, and the words about when
// are core/Review; the pane draws them. A click on a title emits signal_goto
// and the Shell decides what that means, as Today, the Inbox and Tags do.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class ProjectsPane : public widgets::Box {
public:
    explicit ProjectsPane(std::string_view name);

    void set_source(core::NodeSource* src);
    void refresh();

    // Review (true) or All (false). The Shell flips it on a second
    // Ctrl+Shift+P; the buttons are the other writer and call this too.
    void set_review_mode(bool review);
    bool review_mode() const { return m_review; }

    // The first project due a look, "" when none -- Mark Reviewed (the key)
    // walks on to it, so a review is: read, Ctrl+Shift+R, read the next.
    core::NodeId first_due() const;

    sigc::signal<void(core::NodeId)>& signal_goto() { return m_sig_goto; }
    sigc::signal<void()>& signal_new() { return m_sig_new; }   // s037b: + New project

private:
    void fill_review(std::int64_t now);
    void fill_all(std::int64_t now);
    void add_heading(const std::string& text, const std::string& id, const char* icon);
    Gtk::Widget* project_row(const core::Node& n, const std::string& line1,
                             const std::string& line2, bool reviewed_button, bool dim);

    core::NodeSource* m_src = nullptr;   // not owned; the seam
    bool              m_review = true;

    widgets::Box          m_modes;
    widgets::ToggleButton m_mode_review;
    widgets::ToggleButton m_mode_all;
    widgets::Label        m_summary;
    widgets::ScrolledWindow m_scroll;
    widgets::Box          m_column;
    widgets::Label        m_empty;

    bool m_building = false;        // set_active emits toggled; do not re-enter
    bool m_completed_open = false;  // the folds stay as you left them
    bool m_dropped_open   = false;

    sigc::signal<void(core::NodeId)> m_sig_goto;
    sigc::signal<void()>             m_sig_new;
    widgets::Button                  m_new;   // s037b
};

}  // namespace jot
