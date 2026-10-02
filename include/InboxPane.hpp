#pragma once
#include "core/Nodes.hpp"
#include "widgets/Widgets.hpp"

#include <sigc++/signal.h>

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// InboxPane -- what is waiting to be processed (s028).
//
// The third view in the left pane: Notes | Inbox | Today. It lists every node
// carrying the Inbox mark (core::inbox_members), in tree order, each with the
// one fact the tree cannot show -- how long it has been waiting, or what has
// happened to it.
//
// A PROCESSED ROW STAYS. A capture you have filed under a project, or a todo
// you ticked, is drawn dimmed with "Filed in Taxes" / "Done" and remains on
// the list until Clean Up. The list you are working down does not reflow
// under your hand; Clean Up is the moment it does, and you choose it.
//
// Two verbs, both straight through the model:
//   Move...   on a waiting row (s029) -- signal_move; the Shell opens the
//             picker and files it. The row then reads "Filed in ..." like a drag.
//   Keep      on a waiting row -- "it belongs at the top level"; clears the mark
//   Clean Up  win.clean-up (the Shell owns it: a menu item and a key reach it
//             too) -- clears every processed row's mark
//
// IT DECIDES NOTHING. Waiting / Filed / Done come from core::inbox_state, and
// the pane draws them. Clicking a row emits signal_goto; the Shell decides
// what that means, exactly as Today and the drawer do.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class InboxPane : public widgets::Box {
public:
    explicit InboxPane(std::string_view name);

    void set_source(core::NodeSource* src);
    void refresh();

    sigc::signal<void(core::NodeId)>& signal_goto() { return m_sig_goto; }
    sigc::signal<void(core::NodeId)>& signal_move() { return m_sig_move; }   // s029

private:
    Gtk::Widget* row(const core::Node& n, std::int64_t now);

    core::NodeSource* m_src = nullptr;   // not owned; the seam

    widgets::Box            m_bar;
    widgets::Label          m_summary;
    widgets::Button         m_clean;
    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_column;
    widgets::Label          m_empty;

    sigc::signal<void(core::NodeId)> m_sig_goto;
    sigc::signal<void(core::NodeId)> m_sig_move;
};

}  // namespace jot
