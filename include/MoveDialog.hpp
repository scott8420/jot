#pragma once
#include "core/Filing.hpp"
#include "core/Nodes.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/listboxrow.h>
#include <gtkmm/window.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// MoveDialog -- "Move to..." (s029). The processing verb the Inbox lacked.
//
// A small modal window: what is being moved, a search line, and the places it
// may go. Typing narrows the list; Up / Down walk it; Enter or a click files
// the note and closes. With nothing typed, the few places you filed into last
// come first ("Recent"), because processing an Inbox is mostly the same three
// projects over and over.
//
// IT DECIDES NOTHING. Which places are legal, how they rank and how a place is
// named come from core/Filing (selftested); the move itself is the Shell's,
// through the NodeSource like every other write. This window only draws a list
// and reports a choice -- `done(target)`, where an empty target is the top
// level. Cancel, Escape and the X report nothing at all.
//
// Built per use and held by the Shell (one at a time), like the naming dialog:
// the note being moved is fixed at construction.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class MoveDialog : public Gtk::Window {
public:
    using Done = std::function<void(const core::NodeId& target)>;

    MoveDialog(Gtk::Window& parent, const core::NodeSource& src, const core::NodeId& moving,
               std::vector<core::NodeId> recent, Done done);
    ~MoveDialog() override;

    // The cap on drawn rows. Thousands of rows per keystroke is the one way
    // this list could get slow, and nobody scrolls past two hundred to file a
    // note -- they type one more letter. The footer says so.
    static constexpr std::size_t kMaxRows = 200;

private:
    void rebuild();                           // the list, from the query
    void add_header(const std::string& text);
    void add_target(const core::MoveTarget& t);
    void step(int delta);                     // Up / Down from the search line
    void accept();                            // the selected row, or nothing

    const core::NodeSource&   m_src;
    core::NodeId              m_moving;
    std::vector<core::NodeId> m_recent;
    Done                      m_done;

    widgets::Label          m_heading;
    widgets::Entry          m_search;
    widgets::ScrolledWindow m_scroll;
    widgets::ListBox        m_list;
    widgets::Label          m_footer;
    widgets::Button         m_accept;

    // The selectable rows and the place each one means. Headers are not here,
    // which is how step() knows to walk past them.
    std::vector<std::pair<Gtk::ListBoxRow*, core::NodeId>> m_rows;
};

}  // namespace jot
