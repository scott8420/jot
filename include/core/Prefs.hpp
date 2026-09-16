#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Prefs -- the app's own small state, on the Recents pump's shape.
//
// jot had no prefs file until s006, and the drawer needs one: a pane you can
// hide is a pane that has to come back the way you left it, or hiding it is a
// thing you do once and then re-do every launch.
//
// DELIBERATELY A SECOND PUMP, not a new mechanism. Same file as Recents in
// spirit -- encode and decode adjacent so a write cannot skew from its read,
// tolerant of a missing or unparseable file, the CALLER resolves the XDG path
// and hands in a plain string so this stays GTK-free. Inventing a settings
// framework here would be building a cathedral for four fields.
//
// NOT GSettings, and that is worth one line: GSettings wants a compiled schema
// installed system-wide, which turns "run the build" into "install the app" for
// a project whose whole loop is Scott running ./build/jot out of a tree. A JSON
// file in the data dir costs nothing and moves with the notes.
//
// This is also where autosave-on/off lands when it exists -- s005's close prompt
// is written as the never-saved-at-all case and the general case slots into the
// same close handler.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

struct Prefs {
    // The two toggles. Both false is the FOCUS MODE -- the note alone on
    // screen, which is the front door ARCHITECTURE describes rather than a side
    // effect of having toggles. The drawer starts closed because a scribble pad
    // opens into a note, not into metadata about one.
    bool show_tree   = true;
    bool show_drawer = false;

    // Pane positions, in pixels. Remembered even while a pane is hidden, which
    // is the whole reason they are stored separately from the flags: a
    // collapsed Gtk::Paned child reports a position of its own and writing that
    // back is how the real width gets lost for good.
    int tree_width  = 280;   // paned_left position
    int note_width  = 560;   // paned_right position (note | drawer split)

    // s009: write dated todos into a GNOME task list so they show up in the
    // calendar drop-down. OFF by default, and not as caution-by-reflex: it is
    // the one thing jot does that writes OUTSIDE its own jots folder, into
    // another application's database. A feature like that is one a user turns
    // on, not one they discover has already happened.
    bool desktop_tasks = false;

    // s011: a notification when a due task arrives. ON by default, unlike the
    // projection above, and the difference is what each one touches: the
    // projection WRITES INTO another application's database, which is a thing a
    // user turns on rather than discovers. A notification writes nothing, it is
    // the core job of a deadline to be announced, and GNOME already owns a
    // per-application off switch for it in Settings.
    bool notify_due = true;

    // s012: keep the process alive when the window is closed, so the minute
    // clock above keeps ticking. OFF by default, and this one is not a judgement
    // call about caution -- it is about what the X means. Every other
    // application on the machine exits when you close its last window, and a
    // jot that came up ON by default would leave a user who closed it running a
    // process they did not ask for and cannot see without knowing where GNOME
    // hides the list. On by default is the thing the Background Apps menu
    // exists to catch people doing.
    bool background = false;

    // ── the second fingerprint, and why it is a LIST ───────────────────────
    // m_desktop_sig remembers what the desktop was last SENT -- one string,
    // because the projection is rewritten whole every time. This remembers what
    // has already been SAID, which cannot work that way: a notification is an
    // event, so the memory has to be per-task and it has to survive a restart
    // or every launch re-announces the same overdue task.
    //
    // Pruned on every check to exactly the set that is currently due (see
    // core::due_announcements), so it tracks the open deadlines rather than
    // growing for the life of the jots folder.
    std::vector<std::string> announced;

    // ── the window itself ──────────────────────────────────────────────────
    // SIZE AND MAXIMIZED ONLY, AND NOT POSITION. GTK4 removed window
    // positioning outright -- there is no `gtk_window_move`, and Wayland gives
    // an application no way to ask where it is or to put itself somewhere. So
    // "remember it was on the left-hand monitor" is not a thing jot declined to
    // build; it is a thing the platform does not offer at any price. The
    // compositor places the window and jot says how big.
    //
    // The unmaximized size is kept separately from the maximized flag because a
    // maximized window reports the SCREEN's size: writing that back would mean
    // un-maximizing gave you a window the size of the display, and the real
    // size would be lost the first time you ever maximized.
    int  win_width     = 940;
    int  win_height    = 620;
    bool win_maximized = false;
};

// Round-trip fidelity is the bar (CANON): what you save is what you load.
// load is first-run tolerant -- a missing file is defaults, not a failure, and
// an unparseable one is defaults too rather than an exception across the seam.
Prefs load_prefs(const std::string& file);
bool  save_prefs(const std::string& file, const Prefs& p);

}  // namespace jot::core
