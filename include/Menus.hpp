#pragma once
#include <giomm/menu.h>

// ─────────────────────────────────────────────────────────────────────────────
// Menus -- the note menu, built in ONE place (s016c).
//
// The note verbs used to be written out twice: once in the hamburger and once
// in the tree's right-click menu, with labels that had already drifted apart
// ("New note under selection" there, "New note under this" here). Now both the
// header's note button and the tree's context menu take this model, so they
// cannot disagree -- one list, two consumers, the shape core::Shortcuts has
// had since s001.
//
// The labels are short and say a STATE where the action is a toggle: "Todo",
// "Done", "Flagged", "Protected" are stateful win.* actions, so GTK draws them
// as check items and the tick says which way the toggle will go. That is what
// replaced "Make a todo / not a todo" and "Protect / unprotect" -- a label that
// has to describe both directions is a label that describes neither.
//
// Everything here names win.* actions and holds no state: sensitivity and ticks
// come from Shell::update_note_actions(), the single writer.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::menus {

Glib::RefPtr<Gio::Menu> note_menu();

}  // namespace jot::menus
