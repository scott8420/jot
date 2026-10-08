#pragma once
#include "widgets/Widgets.hpp"

#include <gtkmm/box.h>

#include <string>
#include <vector>

namespace jot {

// CheatSheetPage -- core::cheat_sheet()'s GTK consumer (s030, road item 3).
//
// s067: it was a window of its own (CheatSheetWindow); it is now the Cheat
// Sheet page of the Help window, beside the Guide -- one idea ("how do I ..."),
// one window, two pages (Scott: controls that share a concept are combined).
// Nothing about the sheet itself changed.
//
// Decides nothing and hand-lists nothing: it walks core::cheat_sheet() in
// section order, and a line's keys come from the shortcut registry through
// CheatLine::display_how(). The search line filters with core::cheat_matches(),
// the same predicate the selftest proves -- so "what does Ctrl+M do" and "how do
// I file a note" are both a few letters away.
//
// Every row is built ONCE and filtering only shows / hides it: the sheet is a
// few dozen lines, and a rebuild per keystroke would re-register names for no
// gain. A heading hides when nothing under it matches.
class CheatSheetPage : public widgets::Box {
public:
    CheatSheetPage();

    // A fresh look: `query` in the search (usually ""), scrolled to the top.
    void reset(const std::string& query);
    void focus_search();
    // Esc on this page: empties a search and returns true; with nothing typed,
    // returns false (the window closes).
    bool clear_search();

    // s070: keys changed -- the rows say them again (the search stays).
    void rebuild();

    // For the trace channel: how many lines the current query shows.
    int visible_lines() const { return m_visible; }

    static constexpr int kHowWidth = 200;   // the left column, px

private:
    void build();
    void filter();

    struct Row {
        Gtk::Widget* widget = nullptr;
        std::size_t  line = 0;      // index into core::cheat_sheet()
        std::size_t  section = 0;   // index into m_headings
    };

    widgets::Entry          m_search;
    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_column;
    widgets::Label          m_footer;

    std::vector<Gtk::Widget*>    m_headings;   // one per core::cheat_sections() entry
    std::vector<Row>             m_rows;
    int                          m_visible = 0;
};

}  // namespace jot
