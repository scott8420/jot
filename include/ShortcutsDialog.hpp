#pragma once
#include "core/Shortcuts.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/window.h>
#include <sigc++/signal.h>

#include <string>
#include <vector>

namespace jot {

// ShortcutsDialog -- the Keyboard Shortcuts window (Ctrl+?, main menu).
//
// s069 (Scott: "most Gnome apps have a dedicated hotkey dialog for the user to
// memorize"). s001's plain grid, redone the GNOME way: a card per section,
// the cards flowing into columns, each row its words on the left and its keys
// as KEYCAPS on the right ("or" between alternatives); a search in the title
// bar -- typing anywhere starts it -- that narrows the rows, and lights the
// caps of a row found by its keys ("ctrl+m", "f1"). Keys only: the drags and
// clicks stay on the cheat sheet, and the foot links there and to the guide.
//
// Still the SHORTCUT-REGISTRY paradigm's consumer: it renders core::key_rows(),
// which reads core::shortcut_registry() -- the list the accelerators are wired
// from -- so a key and its row cannot drift. Custom Gtk::Window, NOT
// GtkShortcutsWindow (deprecated in GTK 4.18, gone in GTK 5).
//
// Hide-on-close singleton (CANON: "Lifetime shape is design"): rows built once,
// filtering only shows and hides them.
class ShortcutsDialog : public Gtk::Window {
public:
    ShortcutsDialog();
    ~ShortcutsDialog() override;
    void show(Gtk::Window& parent);   // a fresh look: empty search, at the top

    int visible_rows() const { return m_visible; }   // for the trace channel

    // The foot's links -- the Shell opens Help on that page.
    sigc::signal<void()>& signal_cheat_sheet() { return m_sig_cheat; }
    sigc::signal<void()>& signal_guide() { return m_sig_guide; }

private:
    void build();
    void filter();

    struct Row {
        Gtk::Widget*              widget = nullptr;
        Gtk::Widget*              sep = nullptr;    // the hairline above it (none for a card's first)
        std::vector<Gtk::Widget*> caps;
        std::vector<std::size_t>  cap_alt;          // which alternative each cap is in
        std::size_t               row = 0;          // index into m_rows_data
        std::size_t               card = 0;
    };

    widgets::SearchEntry    m_search;
    widgets::ScrolledWindow m_scroll;
    widgets::FlowBox        m_cards;
    widgets::Label          m_none;

    std::vector<core::KeyRow>  m_rows_data;
    std::vector<Row>           m_rows;
    std::vector<Gtk::Widget*>  m_card_widgets;   // the FlowBoxChild of each card
    int                        m_visible = 0;

    sigc::signal<void()> m_sig_cheat, m_sig_guide;
};

}  // namespace jot
