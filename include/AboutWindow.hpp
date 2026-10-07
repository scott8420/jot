#pragma once
#include <gtkmm/window.h>

// ─────────────────────────────────────────────────────────────────────────────
// AboutWindow -- what jot is, who made it, and what it stands on.
//
// s058b (Scott: "the about dialog seems weak ... it should describe Jot better
// and an additional credits"). Three pages behind a switcher in the title bar,
// the Mac way:
//
//   About    the pixie, the version, what jot is for, and what it does -- one
//            row per part of the app, each with that part's own tab icon.
//   Credits  who made it, the logo, the projects it grew from, the ideas it
//            borrows, the libraries it is built on (the same three lists as
//            THIRD_PARTY.md -- keep them in step), and what it is running on.
//   Licence  the MIT text and the link to the repository.
//
// Still the dialog-lifetime exemplar it was born as: a custom Gtk::Window
// (Gtk::AboutDialog is deprecated in GTK4), a hide-on-close SINGLETON (CANON:
// "Lifetime shape is design") -- Shell builds it once and re-presents it, so its
// named children never re-register. Esc closes it.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class AboutWindow : public Gtk::Window {
public:
    explicit AboutWindow(Gtk::Window& parent);
    ~AboutWindow() override;
};

}  // namespace jot
