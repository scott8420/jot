#pragma once
// core::CheatSheet -- the RUNNING cheat sheet (s030, road item 3).
//
// What the shortcuts window is for keys, this is for the whole app: every verb,
// gesture, markdown mark, view and command-line form a person can use, one line
// each, in the order someone meets them (capture -> Inbox -> file -> do).
//
// Two stones it carries:
//
//   1. A KEY IS NEVER WRITTEN HERE TWICE. A line that names a GAction takes its
//      keys from core::shortcut_registry() at display time, so the cheat sheet,
//      the shortcuts window and the accelerator table are one fact read three
//      ways. Change Ctrl+M and all three change; nothing here can be stale.
//
//   2. "THE CHEAT SHEET RUNS" IS A FAILING TEST, NOT A THING TO REMEMBER. The
//      selftest demands a line for every keyed verb in the shortcut registry
//      (Diagnostics aside). A milestone that adds a key and forgets its line
//      ships red. RULES says every milestone adds its lines; this is the teeth.
//
// GTK-free, like the shortcut registry, so the filter and the coverage rule are
// proved headless.
#include <string>
#include <vector>

namespace jot::core {

struct CheatLine {
    std::string section;   // one of cheat_sections(), contiguous, in that order
    std::string action;    // "win.move-to" -> keys come from the shortcut registry
    std::string keys;      // literal "how" when there is no action (a gesture, a mark,
                           // a command); shown AFTER the registry keys if both are set
    std::string what;      // what it does -- the line a reader scans
    std::string where;     // optional: the other ways in, or the one caveat that matters

    // The left column: the registry's keys for `action` (if any), else `keys`;
    // both joined with "  or  " when a line has a key AND a second way.
    std::string display_how() const;
};

// The sections, in reading order -- the order someone meets them, not A-Z.
const std::vector<std::string>& cheat_sections();

// Every line, grouped by section in cheat_sections() order.
const std::vector<CheatLine>& cheat_sheet();

// True when `query` (trimmed, ASCII case-blind; every word must hit) appears in
// the line's section, how, what or where. An empty query matches everything.
// "inbox move" finds the Move line; "ctrl+m" finds it by its key.
bool cheat_matches(const CheatLine& line, const std::string& query);

// The coverage rule, as data: every registry action with a key (Diagnostics
// aside) that no cheat line names. Empty => the sheet is complete.
std::vector<std::string> cheat_missing_actions();

// Cheat lines that name an action the shortcut registry does not know -- a typo
// here would otherwise show a line with no key and no complaint.
std::vector<std::string> cheat_unknown_actions();

}  // namespace jot::core
