#pragma once
#include "core/Nodes.hpp"
#include "core/Search.hpp"
#include "widgets/Widgets.hpp"

#include <sigc++/signal.h>

#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// SearchPane -- the results of the Find field over the Notes tab (s038).
//
// While the field has words in it, this stands where the tree stands: one row
// per note that matches, names first, each with where it lives and the line it
// matched on. Empty the field and the tree comes back as you left it (a Stack,
// as the left pane itself is).
//
// IT DECIDES NOTHING: the hits are core::search. A row click emits
// signal_open(id, cp_start, cp_len) and the Shell opens the note there.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class SearchPane : public widgets::Box {
public:
    explicit SearchPane(std::string_view name);

    void set_source(core::NodeSource* src);
    void set_query(const std::string& q);   // re-runs the search
    void refresh();                         // same query, the model changed
    const std::string& query() const { return m_query; }

    // Enter in the field: the first hit, if any. False when there is none.
    bool open_first();

    sigc::signal<void(core::NodeId, int, int)>& signal_open() { return m_sig_open; }

private:
    Gtk::Widget* hit_row(const core::SearchHit& h);

    core::NodeSource* m_src = nullptr;
    std::string       m_query;
    std::vector<core::SearchHit> m_hits;

    widgets::Label          m_summary;
    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_column;

    sigc::signal<void(core::NodeId, int, int)> m_sig_open;
};

}  // namespace jot
