#pragma once
#include "widgets/Widgets.hpp"

#include <gtkmm/window.h>
#include <sigc++/signal.h>

#include <string>
#include <vector>

namespace jot {

class CheatSheetPage;

// HelpWindow -- jot Help (s067, J6).
//
// Scott, Oct: jot "needs in-app help to guide users in getting the most out of
// it". One window, two pages behind a switcher in the title bar (the About
// window's shape, the Mac place for a window's tabs):
//
//   Guide        a page per IDEA -- what the Inbox, Today, done-when, the
//                timeline ... are for and how to use them -- listed down a
//                source-list sidebar in reading order, with a search over the
//                pages. Each page can DO: "Try it" buttons run the verb it is
//                about, "Every key for this" opens the Cheat Sheet filtered to
//                the page, See also and Previous / Next walk on.
//   Cheat Sheet  every key and gesture, one line each (s030's sheet, moved in).
//
// Keys: F1 / Ctrl+Shift+H open the Guide (F1 is GNOME's Help); Ctrl+H the Cheat
// Sheet, as since s030. Main menu › jot Help.
//
// DECIDES NOTHING: the pages are core::help_topics(), the keys on a Try come
// from the shortcut registry, and a Try is handed to the Shell as a detailed
// action name (signal_try) -- the Shell runs it on the main window, so the help
// window never reaches into the app. Hide-on-close singleton, like About.
class HelpWindow : public Gtk::Window {
public:
    HelpWindow();
    ~HelpWindow() override;

    // Present on the Guide, at `topic` (or wherever the reader last was).
    void show_guide(Gtk::Window& parent, const std::string& topic = "");
    // Present on the Cheat Sheet, its search holding `query`.
    void show_keys(Gtk::Window& parent, const std::string& query = "");

    const std::string& current_topic() const { return m_topic; }
    // s068: start on this page (the one kept in prefs); unknown ids are ignored.
    void set_topic(const std::string& id) { if (!id.empty()) show_topic(id); }
    // The reader turned to another page -- the Shell keeps it in prefs.
    sigc::signal<void(std::string)>& signal_page() { return m_sig_page; }
    int visible_lines() const;   // the cheat sheet's, for the trace channel

    // A Try was pressed: "win.timeline", "win.left-view::inbox" ...
    sigc::signal<void(std::string)>& signal_try() { return m_sig_try; }

private:
    void build_guide();
    void fill_sidebar();
    void filter_sidebar();
    void show_topic(const std::string& id);
    void fill_page(const std::string& id);
    bool on_escape();

    widgets::Stack          m_stack;
    widgets::StackSwitcher  m_switcher;
    widgets::SearchEntry    m_find;      // over the sidebar: find a page
    widgets::ListBox        m_list;      // the sidebar: headings and pages
    widgets::ScrolledWindow m_page_scroll;
    widgets::Box            m_page;      // the page on show -- rebuilt per topic
    widgets::Label          m_no_match;
    CheatSheetPage*         m_cheat = nullptr;   // managed, owned by m_stack

    struct SideRow {
        Gtk::ListBoxRow* row = nullptr;
        std::string      topic;    // "" for a group heading
        std::size_t      group = 0;
    };
    std::vector<SideRow> m_rows;
    std::string          m_topic = "welcome";
    bool                 m_selecting = false;   // a programmatic select, not the reader

    sigc::signal<void(std::string)> m_sig_try;
    sigc::signal<void(std::string)> m_sig_page;
};

}  // namespace jot
