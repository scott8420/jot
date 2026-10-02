#include "core/CheatSheet.hpp"
#include "core/Shortcuts.hpp"

#include <algorithm>
#include <cctype>
#include <set>

// CheatSheet.cpp -- the lines. See the header for the two rules: keys come
// from the shortcut registry, and the selftest fails on a keyed verb with no
// line. ADD A MILESTONE'S LINES HERE IN THE SAME SHIP AS THE MILESTONE.
//
// House style for a line: `what` is a short imperative-ish sentence a reader
// scans; `where` is the second way in or the one caveat, never a paragraph.

namespace jot::core {

namespace {

const ShortcutSpec* spec_for(const std::string& action) {
    for (const auto& s : shortcut_registry())
        if (s.action == action) return &s;
    return nullptr;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> words(const std::string& q) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : q) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) out.push_back(lower(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(lower(cur));
    return out;
}

}  // namespace

std::string CheatLine::display_how() const {
    std::string reg;
    if (!action.empty())
        if (const ShortcutSpec* s = spec_for(action)) reg = s->display_keys();
    if (reg.empty()) return keys;
    if (keys.empty()) return reg;
    return reg + "  or  " + keys;
}

const std::vector<std::string>& cheat_sections() {
    static const std::vector<std::string> kSections = {
        "Capture", "Inbox", "Notes", "Todos", "Projects", "Today", "Writing",
        "Files and pictures", "Views", "Command line", "Jots folders", "Help",
    };
    return kSections;
}

const std::vector<CheatLine>& cheat_sheet() {
    // Fields: {section, action, keys, what, where}
    static const std::vector<CheatLine> kLines = {
        // ── Capture ─────────────────────────────────────────────────────────
        {"Capture", "win.capture", "", "Jump to the capture line in the header",
         "Type a thought, press Enter: a new note in the Inbox. The cursor stays put "
         "for the next one."},
        {"Capture", "", "Your own key", "Capture from anywhere on the desktop, jot open or not",
         "Set it in Preferences › Capture › Global capture shortcut."},
        {"Capture", "", "jot --capture \"text\"", "Capture from a terminal or a script",
         "With jot closed it is kept and filed the next time jot opens."},

        // ── Inbox ───────────────────────────────────────────────────────────
        {"Inbox", "", "Inbox tab", "Everything captured and not dealt with yet",
         "Side pane: Notes | Inbox | Today. The tab counts what is waiting."},
        {"Inbox", "win.move-to", "", "Move: file the note — type where it goes, Enter",
         "Also Move… on an Inbox row, and ⋮ › Move to… — "
         "your recent places come first."},
        {"Inbox", "", "Keep", "It belongs where it is: take it off the waiting list",
         "The button on an Inbox row."},
        {"Inbox", "win.clean-up", "", "Clean Up: clear the filed and done rows off the Inbox",
         "They stay, dimmed, until you do. Nothing is moved or deleted."},
        {"Inbox", "", "⋮ › In Inbox", "Put any note on the Inbox by hand, or take it off",
         ""},

        // ── Notes ───────────────────────────────────────────────────────────
        {"Notes", "win.new-note", "", "New note at the top level", ""},
        {"Notes", "win.new-child", "", "New note under the selected one",
         "Any note can hold other notes."},
        {"Notes", "win.rename-note", "", "Rename the selected note", ""},
        {"Notes", "", "Drag onto a row", "Make it a child of that note",
         "A row's top or bottom edge: a sibling above or below. Below the list: "
         "the top level."},
        {"Notes", "win.copy-link", "", "Copy a link to this note",
         "Paste it into another note. The link survives a rename and a move."},
        {"Notes", "", "Ctrl+click a link", "Follow it",
         "In Source and Live Preview. In Reading a plain click follows it."},
        {"Notes", "", "#tag", "Tag a note: type the tag anywhere in its text",
         "Tags show in note details."},
        {"Notes", "win.toggle-protect", "", "Protect: lock a note against edits, moves and deletes",
         "⋮ › Protected."},
        {"Notes", "", "Delete (in the tree)", "Delete the note and everything under it",
         "Or ⋮ › Delete. A protected note, or one holding one, is refused."},

        // ── Todos ───────────────────────────────────────────────────────────
        {"Todos", "win.toggle-todo", "", "Make the note a todo, or a plain note again",
         "A todo is still a note: its text, children and links stay."},
        {"Todos", "win.toggle-done", "", "Tick it done, or not", ""},
        {"Todos", "win.toggle-flag", "", "Flag it: “this one, today”", ""},
        {"Todos", "", "Due / Defer", "Give it dates, in note details",
         "2026-10-09, 2026-10-09 14:30, today, tomorrow. Defer hides it until then. "
         "Both pass down to the notes under it."},
        {"Todos", "", "Drag it up", "Set priority: the order IS the priority",
         "There is no priority number."},

        // ── Projects (s031) ─────────────────────────────────────────────────
        {"Projects", "", "Any note with notes under it",
         "Is a project: its todos are the steps", "It need not be a todo itself."},
        {"Projects", "", "Sequential / Parallel", "One step at a time, or all at once",
         "Note details › Structure › Children. Sequential offers only the first step not done."},
        {"Projects", "", "Single actions",
         "A list of loose todos: all available, nothing comes first",
         "Note details › Structure › Children. For errands, not for a plan."},
        {"Projects", "", "⋮ › Project › On Hold",
         "Pause a project: its todos leave Today and Available",
         "A late or flagged one still shows, marked On hold. Also Note details › Structure › Status."},
        {"Projects", "", "⋮ › Project › Dropped",
         "Give up on it: kept, but out of every list and the calendar",
         "Struck through in the tree. Active brings everything back."},
        {"Projects", "", "⋮ › Project › Completed",
         "Finish it: everything inside counts as done",
         "On a project that is itself a todo, this is the same as ticking it."},

        // ── Today ───────────────────────────────────────────────────────────
        {"Today", "", "Today tab", "What you can actually do now",
         "Today: due by tonight, plus flagged. Available: everything not blocked or "
         "deferred. Flagged: all flagged."},
        {"Today", "", "Today tab › Logbook", "What got done, newest first, by day",
         "Ticked todos, and projects Completed or Dropped, with the time. Untick one there "
         "and it goes back."},
        {"Today", "", "Due notification", "A desktop notice when a todo comes due",
         "Click it to open the todo. On in Preferences; jot can keep running with "
         "its window closed so the clock keeps ticking."},
        {"Today", "", "Calendar", "Dated todos in GNOME's top-bar calendar",
         "Today pane › Show dated todos on the desktop. One way: jot writes, the "
         "calendar only shows."},

        // ── Writing ─────────────────────────────────────────────────────────
        {"Writing", "", "Format bar", "Buttons above the note that write the markdown for you",
         "Every button toggles: Bold on bold text unbolds it. Hidden in Reading."},
        {"Writing", "", "Ctrl+B / Ctrl+I", "Bold / italic — or undo it", "**bold**  *italic*"},
        {"Writing", "", "Ctrl+K", "Link: the selection becomes its label", "[label](address)"},
        {"Writing", "", "# Heading", "A heading; ## and ### are smaller",
         "Or the format bar's Heading menu."},
        {"Writing", "", "- item", "A bullet list",
         "Enter starts the next item; Enter on an empty item ends the list."},
        {"Writing", "", "Tab / Shift+Tab", "Nest a list item under the one above, or bring it back",
         "On a list line. Anywhere else Tab is a plain tab."},
        {"Writing", "", "1. item", "A numbered list — it renumbers itself", ""},
        {"Writing", "", "- [ ] item", "A checkbox line; click the box to tick it",
         "Becomes - [x] when ticked."},
        {"Writing", "", "> quote", "A quote", ""},
        {"Writing", "", "```", "A code block: three backticks above and below",
         "Reading and Live Preview give it a Copy button."},
        {"Writing", "", "`code`  ~~strike~~  ---", "Inline code, strikethrough, a dividing line",
         ""},
        {"Writing", "", "Ctrl+Z", "Undo — one format or one list step at a time", ""},

        // ── Files and pictures ──────────────────────────────────────────────
        {"Files and pictures", "", "Drop a file on the note",
         "Put it in the note where it lands",
         "Copied into the jots folder. A picture is drawn in place; any other file is a link."},
        {"Files and pictures", "", "Shift while dropping", "Link to the file instead of copying it",
         "Or the reverse, if Preferences says to link dropped files."},
        {"Files and pictures", "", "Ctrl+V a picture", "Paste a screenshot into the note", ""},
        {"Files and pictures", "", "Note details › Enclosures",
         "What the note carries: Open, Show in Files, Save a Copy",
         "A file that is gone says Missing; a linked one that changed says Modified."},
        {"Files and pictures", "win.import-md", "", "Import markdown files as new notes",
         "Or drag .md files onto the tree: on a row, they go under it."},
        {"Files and pictures", "win.import-md-folder", "Main menu",
         "Import a whole folder of markdown", "Subfolders become notes holding their files."},

        // ── Views ───────────────────────────────────────────────────────────
        {"Views", "win.toggle-reading", "", "Reading: the note without its marks",
         "Double-click anywhere to go back to editing at that spot."},
        {"Views", "win.toggle-live", "", "Live Preview: marks show only where the cursor is",
         "Off: every mark on screen (plain Source)."},
        {"Views", "win.toggle-tree", "", "Show or hide the side pane", ""},
        {"Views", "win.toggle-drawer", "", "Show or hide note details",
         "Both panes off: the note alone on screen."},

        // ── Command line ────────────────────────────────────────────────────
        {"Command line", "", "jot NAME -l milk eggs \"rye bread\"",
         "Add task lines to the note NAME", "Made if there is no note of that name."},
        {"Command line", "", "jot NAME -a some words", "Add a line of text to the note NAME",
         "The reply says Made or Added, so a mistyped name shows."},
        {"Command line", "", "jot --capture", "Open jot on the capture line", ""},
        {"Command line", "", "JOT_DEBUG=info:all jot", "A talkative console, for chasing a problem",
         "Quiet (warnings only) without it."},

        // ── Jots folders ────────────────────────────────────────────────────
        {"Jots folders", "win.new-jots", "", "Start a new jots folder", ""},
        {"Jots folders", "win.open-jots", "", "Open a jots folder",
         "Main menu › Recent jots for the ones you have used."},
        {"Jots folders", "win.save-all", "", "Save now",
         "jot saves by itself; this just does it this instant."},
        {"Jots folders", "win.save-as", "", "Copy everything to a new jots folder and switch to it",
         ""},
        {"Jots folders", "", "The folder name in the header",
         "Open in Files, Copy path, Rename or Relocate the jots folder", ""},

        // ── Help ────────────────────────────────────────────────────────────
        {"Help", "win.cheat-sheet", "", "This cheat sheet — type to find a line", ""},
        {"Help", "win.shortcuts", "", "Every keyboard shortcut in one list", ""},
        {"Help", "win.preferences", "", "Preferences",
         "Capture key, notifications, the calendar, keep running, linking drops."},
        {"Help", "win.quit", "", "Quit",
         "Closing the window can leave jot running for notifications (Preferences)."},
    };
    return kLines;
}

bool cheat_matches(const CheatLine& line, const std::string& query) {
    const auto ws = words(query);
    if (ws.empty()) return true;
    const std::string hay = lower(line.section + "\n" + line.display_how() + "\n" +
                                  line.what + "\n" + line.where);
    return std::all_of(ws.begin(), ws.end(),
                       [&](const std::string& w) { return hay.find(w) != std::string::npos; });
}

std::vector<std::string> cheat_missing_actions() {
    std::set<std::string> named;
    for (const auto& l : cheat_sheet())
        if (!l.action.empty()) named.insert(l.action);
    std::vector<std::string> out;
    for (const auto& s : shortcut_registry()) {
        if (s.action.empty() || s.accels.empty()) continue;   // doc-only or unkeyed
        if (s.section == "Diagnostics") continue;              // for us, not for users
        if (!named.count(s.action)) out.push_back(s.action);
    }
    return out;
}

std::vector<std::string> cheat_unknown_actions() {
    std::vector<std::string> out;
    for (const auto& l : cheat_sheet())
        if (!l.action.empty() && !spec_for(l.action)) out.push_back(l.action);
    return out;
}

}  // namespace jot::core
