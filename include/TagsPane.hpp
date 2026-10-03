#pragma once
#include "core/Nodes.hpp"
#include "core/Tags.hpp"
#include "widgets/Widgets.hpp"

#include <sigc++/signal.h>

#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// TagsPane -- tags as contexts (s035, road item 7).
//
// The fourth view in the left pane: Notes | Inbox | Today | Tags. At the top,
// every tag in the folder as a chip -- the PRESSED chip is the filter, so the
// widget shows its own state (CANON: one concept, one widget). Below it, what
// that tag gathers, in three groups:
//
//   Available   todos you can do now -- tick them here, as in Today
//   Waiting     todos deferred, blocked by a sequence, or on hold, each saying why
//   Notes       everything else carrying the tag
//
// Done and dropped todos are counted in the summary line and not listed: the
// Logbook is where finished work lives, and a context view is about what is
// left to do there.
//
// IT DECIDES NOTHING. The chips are core::tag_list, the groups are
// core::tag_members, the why is core::waiting_reason. The pane draws them.
// Clicking a row emits signal_goto and the Shell decides what that means,
// exactly as Today and the Inbox do.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class TagsPane : public widgets::Box {
public:
    explicit TagsPane(std::string_view name);

    void set_source(core::NodeSource* src);
    void refresh();

    // Pick a tag (by name or key; folded here). The Shell calls this when a tag
    // is clicked in the drawer or the note. Empty clears the pick.
    void select(const std::string& tag);
    const std::string& selected() const { return m_key; }

    // s036: put the cursor in the search field over the chips (Ctrl+Shift+T
    // when there is nothing to pick for you).
    void focus_search();

    sigc::signal<void(core::NodeId)>& signal_goto() { return m_sig_goto; }

private:
    void fill_chips(std::int64_t now);
    void draw_chips();                   // s036: m_list through the query
    void fill_members(std::int64_t now);
    void add_heading(const std::string& text, const std::string& id);
    Gtk::Widget* todo_row(const core::Node& n, const std::string& sub, bool dim);
    Gtk::Widget* note_row(const core::Node& n);

    core::NodeSource* m_src = nullptr;   // not owned; the seam
    std::string       m_key;             // the picked tag's KEY, "" for none

    // s036: the search over the chips. Typing narrows them; Enter picks an
    // exact or lone match; Esc clears. The PRESSED chip is never hidden by it:
    // it is the filter's state, and a state you cannot see is a trap.
    widgets::SearchEntry    m_search;
    widgets::Label          m_nomatch;
    std::string             m_query;     // core::tag_query of the field
    std::vector<core::TagInfo> m_list;   // the last tag_list, for typing and Enter

    widgets::ScrolledWindow m_chip_scroll;
    widgets::FlowBox        m_chips;
    widgets::Label          m_summary;
    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_column;
    widgets::Label          m_empty;

    // True while chips are being built / pressed programmatically: a pressed
    // chip being set emits toggled, and that must not re-enter select().
    bool m_building = false;
    bool m_done_open = false;   // s035b: the Done fold stays as you left it, across refreshes

    sigc::signal<void(core::NodeId)> m_sig_goto;
};

}  // namespace jot
