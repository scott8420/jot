#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// core/Lifecycle -- what a close means, and what a quit means.
//
// THE SAME SEAM AS Projection/Desktop AND Notify/Shell, one surface further
// on: the DECISION is pure and lives here, the DOING is GTK and lives in the
// Shell. s009 proved why -- the delivery half was rewritten wholesale in s010
// and the decision half did not change a line.
//
// It earns the seam for a sharper reason here than in either of those. Closing
// a window used to mean one thing, so the branch could live inline in the
// close handler and be read at a glance. With a background preference it means
// three, the difference between two of them is WHETHER UNSAVED NOTES ARE ABOUT
// TO BE LOST, and the wrong answer is silent. A truth table that a headless
// test can drive is worth more than a comment promising the branch is right.
//
// ── the thing that is easy to get backwards ─────────────────────────────────
// Under residency, CLOSING stops being the moment work is at risk and QUITTING
// becomes it. Today's prompt hangs off the close because close means exit; if
// close means "keep running", that prompt is a lie -- nothing is being lost,
// the notes are still in memory, and asking teaches the user to dismiss a
// dialog that will one day matter. So the ask MOVES to the quit, which is the
// path that has no window to put a dialog on. That move is this file.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// Everything the two decisions below are allowed to look at. A struct rather
// than four bare bools at a call site, because four bools at a call site is how
// two of them end up swapped and the test still passes.
struct Lifecycle {
    bool background = false;  // the preference: keep running with no window
    bool quitting   = false;  // a quit is already under way and is driving this
    bool forced     = false;  // the scratch prompt has been answered; do not re-ask
    bool scratch    = false;  // unsaved notes, held only in memory
};

// What a window-close request means.
enum class OnClose {
    Exit,          // let the window go -- with no other windows, the app ends
    StayResident,  // hide it; the process and its clock keep going
    AskFirst,      // unsaved notes are about to be lost; prompt, hold the close
};

OnClose on_close(const Lifecycle& s);

// What a quit request means. There is no StayResident here and that asymmetry
// is the point: a quit is a quit, from the menu, from Ctrl+Q, or from GNOME's
// Background Apps list. The only question left is whether anything is lost.
enum class OnQuit {
    Exit,
    AskFirst,
};

OnQuit on_quit(const Lifecycle& s);

}  // namespace jot::core
