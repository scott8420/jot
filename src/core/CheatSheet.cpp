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
        "Capture", "Inbox", "Notes", "Todos", "Projects", "Today", "Tags", "Writing",
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
         "With jot closed it is kept and filed the next time jot opens. #words become tags."},
        {"Capture", "", "Jot tile", "Capture from GNOME's quick settings panel",
         "The › on the Jot tile opens a line: a thought, or Groceries -l milk eggs, or "
         "Groceries -a words -- as on the command line; #words become tags. What can I type? folds out under it. "
         "./install.sh puts it in (or ./install-extension.sh on its own)."},

        // ── Inbox ───────────────────────────────────────────────────────────
        {"Inbox", "", "Inbox tab", "Everything captured and not dealt with yet",
         "Side pane: Notes | Inbox | Today | Tags | Projects. The tab counts what is waiting."},
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
        {"Notes", "", "#tag", "Tag a note: Note details › Tags, or type #tag anywhere in its text",
         "The Tags tab gathers them."},
        // ── Find (s038) ─────────────────────────────────────────────────────
        {"Notes", "win.find", "Find in all notes", "Every note with these words, in its name or text",
         "Above the tree. Any order, case ignored; #tag works. Names first, then the line each "
         "matched on. Click one: the note opens with the word selected. Enter opens the first; Esc "
         "brings the tree back."},
        // ── Perspectives (s038b) ────────────────────────────────────────────
        {"Notes", "win.perspectives", "Filter and perspectives",
         "The funnel beside Find: show only remaining / available / waiting / done todos, "
         "projects, notes or the Inbox; due by today or within a week; flagged",
         "It writes words into the Find field -- the field is the filter, so you can type them too."},
        {"Notes", "", "#a #b   #a or #b   -#a", "Find: both tags, either tag, not this tag",
         "Words work the same way: paint or primer, -done. `or` joins only its two neighbours."},
        {"Notes", "", "is:available  is:waiting  is:done  is:project  is:inbox  is:flagged",
         "Find: what a note is -- also is:remaining, is:todo, is:note, is:dropped",
         "due:overdue  due:today  due:week  due:any  due:none -- due BY: late ones count."},
        {"Notes", "", "est:30   est:1h   est:none", "Find: todos that take at most that long -- or have no estimate yet",
         "The funnel › Time writes it. With Show › Available: what fits in the time you have."},
        {"Notes", "", "Save as Perspective…", "Keep the Find field's query under a name",
         "The funnel › Perspectives opens it again. Same name replaces; Delete This Perspective "
         "while it is showing."},
        {"Notes", "win.toggle-protect", "", "Protect: lock a note against edits, moves and deletes",
         "⋮ › Protected."},
        {"Notes", "", "Delete (in the tree)", "Delete the note and everything under it",
         "Or ⋮ › Delete. A protected note, or one holding one, is refused."},

        // ── Todos ───────────────────────────────────────────────────────────
        // ── Building up and taking down (s045) ──────────────────────────────
        {"Notes", "", "Ctrl+Z  /  Ctrl+Shift+Z", "Undo / redo any change to notes and todos",
         "Everything but typing: new, delete, move, indent, rename, a tick in the tree or on a "
         "Today / Tags / Find card, flag, every Note details field (name, dates, estimate, repeat, "
         "status, project, packet, tags), the Inbox's Keep and Clean Up, captures, imports, New "
         "project. A deleted note comes back whole, selected. Works anywhere but a text box; also "
         "in the main menu. In the note body (or any text box) Ctrl+Z is that box's own."},
        // s047
        {"Notes", "", "Ctrl+click  /  Shift+click (in the tree)", "Select several notes",
         "Ctrl+click adds or drops one, Shift+click a run, Ctrl+A every row. Delete, Tick, Flag, Make a todo, "
         "Protect, Inbox and Move to\u2026 then act on all of them -- one Ctrl+Z puts them all back. Tick "
         "and Flag turn every one ON unless all already are."},
        // s049
        {"Notes", "", "Note details, several selected", "See and set what they share",
         "A value they all have shows as itself; one they differ on says \u201cmixed\u201d. Set Due, "
         "Defer, Time, Repeat, Done, Flagged or a tag and it goes on all of them -- one Ctrl+Z, and the "
         "group comes back selected. A tag some carry says \u201c2 of 3\u201d: + puts it on the rest."},
        {"Notes", "", "Enter (in the tree)", "New note just below, ready to name",
         "Type its name, Enter, and Enter again for the next one."},
        {"Notes", "", "Tab  /  Shift+Tab (in the tree)", "Indent under the note above / outdent",
         "Or the note's ⋮ menu › Indent, Outdent."},
        {"Notes", "", "Alt+Up  /  Alt+Down (in the tree)", "Move the note up / down among its siblings",
         "Or the note's ⋮ menu › Move Up, Move Down."},
        {"Todos", "win.toggle-todo", "", "Make the note a todo, or a plain note again",
         "A todo is still a note: its text, children and links stay."},
        {"Todos", "win.toggle-done", "", "Tick it done, or not", ""},
        {"Todos", "win.toggle-flag", "", "Flag it: “this one, today”", ""},
        {"Todos", "", "Due / Defer", "Give it dates, in note details",
         "Type 2026-10-09, 2026-10-09 14:30, today, tomorrow -- or the calendar button. "
         "Defer hides it until then; its button offers This weekend and 2 days before due. "
         "Both pass down to the notes under it."},
        {"Todos", "", "Note details › Time", "Say how long a todo takes: 15m, 1h, 1h30",
         "Or pick from the ▾. Rows show it (⏱). Find's funnel › Time finds what fits."},
        {"Todos", "", "Note details › Repeat", "Make a todo come back: weekly, every 2 weeks, monthly",
         "Type it or use the ▾ list. Ticking it logs this one and moves its dates on; ↻ marks it. Tick \u201cCount from "
         "when it is done\u201d for chores."},
        {"Todos", "", "Drag it up", "Set priority: the order IS the priority",
         "There is no priority number."},

        // ── Projects (s031) ─────────────────────────────────────────────────
        {"Projects", "", "A note with todos under it",
         "Is a project: its todos are the steps", "It need not be a todo itself. Or say so on purpose: Is a Project."},
        {"Projects", "", "Sequential / Parallel", "One step at a time, or all at once",
         "Note details › Structure › Children: the steps-going-down or the side-by-side button. Sequential offers only the first step not done."},
        {"Projects", "", "Single actions",
         "A list of loose todos: all available, nothing comes first",
         "Note details › Structure › Children: the loose-squares button. For errands, not for a plan."},
        {"Projects", "", "⋮ › Project › On Hold",
         "Pause a project: its todos leave Today and Available",
         "A late or flagged one still shows, marked On hold. Also Note details › Structure › Status: the pause button."},
        {"Projects", "", "⋮ › Project › Dropped",
         "Give up on it: kept, but out of every list and the calendar",
         "Struck through in the tree. Active brings everything back."},
        {"Projects", "", "⋮ › Project › Completed",
         "Finish it: everything inside counts as done",
         "On a project that is itself a todo, this is the same as ticking it."},

        // ── Review (s037) ───────────────────────────────────────────────────
        {"Projects", "win.show-projects", "Projects tab › Review",
         "The projects due a look, the longest-waiting first",
         "Each comes up once a week. Open it: is the next step right, is anything stuck, "
         "should it go on hold? Then press ✓. Again: All projects."},
        {"Projects", "win.mark-reviewed", "", "Mark Reviewed: this project has had its look",
         "In Review the next one due opens, so you can go straight down the list. Also "
         "⋮ › Project › Mark Reviewed, and Note details › Structure › Review."},
        {"Projects", "win.new-project", "Projects tab › +", "New project: a note that is a project from the start",
         "Name it in the tree, then Ctrl+Shift+N adds its first step."},
        {"Projects", "win.toggle-project", "⋮ › Project › Is a Project",
         "Say a note is a project, or never one",
         "A set of reference notes can be a project. Untick a folder of todos and it never asks for "
         "a review. Also Note details › Project."},
        {"Projects", "", "Note details › Project", "A project's own Due, Defer and Flag",
         "They pass down to the todos in it: its due is theirs, its flag puts them in Flagged."},
        {"Projects", "", "Note details › Review", "How often a project wants a look",
         "Every week unless you say: every 2 weeks, monthly, every 3 days -- or the ▾ list."},
        {"Projects", "", "Projects tab › All projects", "Every project by where it stands",
         "Active, On hold, then Completed and Dropped folded at the foot. A project is a note "
         "with todos directly under it."},

        {"Projects", "", "Projects tab \u203a Routines", "Everything that repeats, and how regularly",
         "Slipped ones first (\u201c2 missed since Sun 20 Sep\u201d, in red), then by the next due. Each "
         "has its last ten as dots -- green on time, orange late, a red ring for each missed -- and "
         "\u201c4 of the last 5 on time\u201d. The same record sits under Repeat in Note details."},
        // ── Done when (s054) ────────────────────────────────────────────────
        {"Projects", "", "Note details \u203a Done when: Every item is in / Every step is done",
         "Say what finished means for a todo or project",
         "Every item is in: each checkbox line in its text, ticked or with a file on it. "
         "Every step is done: each todo directly under it. Cards and the tree show the count "
         "(\u201c2 of 3 in\u201d); ticking it before then asks \u201ctick anyway?\u201d -- Cancel leaves it."},
        // ── Deadline (s055) ─────────────────────────────────────────────────
        {"Projects", "", "Note details \u203a Done when \u203a A deadline",
         "Work back from a due date",
         "jot counts what is left (steps, estimates) against the days left: \u201c3 steps \u00b7 ~2h "
         "\u00b7 9 days out\u201d, then \u201cOn track -- start by Mon 12 Oct\u201d. Each step gets a day "
         "(or each hour of work a day) plus one spare; when the days left come down to that, the "
         "next step goes on Today under \u201cRunning short\u201d. The tree shows \u23f3."},
        {"Projects", "", "Note details \u203a Todo \u203a Feeds: Choose\u2026", "Say what a small step feeds",
         "\u201cscan receipts\u201d feeds \u201cTaxes 2027\u201d: pick the goal (deadlines first). The goal's "
         "Done when then says \u201cFed by 2 feeders \u00b7 ~6h before the due \u00b7 ~15m a week\u201d, a row "
         "each -- red when one has slipped. \u00d7 stops it feeding; Ctrl+Z undoes either."},
        // ── Packets (s044) ──────────────────────────────────────────────────
        {"Projects", "", "Note details › Done when › A packet",
         "Make a note a packet: a set of things the work needs, sent once",
         "Each checkbox line is an item. It is in when a file is dropped onto its line, or "
         "ticked (a paper copy). Note details says \u201c3 of 5 in \u00b7 missing: ...\u201d."},
        {"Projects", "", "Note details \u203a Done when \u203a Gather for sending: Folder\u2026 / Zip\u2026",
         "Send a packet once everything is in",
         "Every item's file is copied into one new folder, or one zip, named for its item "
         "(\u201c01 W-2 (employer).pdf\u201d) with a Contents list that also names the paper "
         "copies. The packet is stamped \u201cSent\u201d with the date; the folder button beside "
         "it shows where it went. Ctrl+Z takes the stamp back (the copies stay)."},
        {"Projects", "", "Note details › Done when › Nudge me",
         "Be reminded what a packet is still missing",
         "Every day / 3 days / week / 2 weeks / month: while something is missing, a "
         "notification after 9:00 says what. In 3 days quiets it; I’ve got it (when only "
         "one is missing) ticks that item; Open goes to it. Stops when it is all in or sent."},
        {"Projects", "", "Note details › Done when › Do it again (once sent)",
         "Next year's packet, from this one",
         "A fresh copy just below -- \u201cTaxes 2026\u201d becomes \u201cTaxes 2027\u201d: the same "
         "items with nothing in, the same nudge, a due date a year on, and \u201cLast time\u201d "
         "linking back. The old one keeps its files and its Sent stamp. One Ctrl+Z. Its repeating feeders feed the new copy."},

        // ── Today ───────────────────────────────────────────────────────────
        {"Today", "", "Today tab", "What you can actually do now",
         "Today: due by tonight, plus flagged. Available: everything not blocked or "
         "deferred. Flagged: all flagged."},
        {"Today", "", "Today tab \u203a Errands (the pin)", "Errands by place, one trip at a time",
         "Tag a todo with a place under #at/ -- #at/town, #at/hardware-store -- and it joins that "
         "place's run: \u201cTown \u00b7 4 errands \u00b7 ~1h 20m \u00b7 2 due this week\u201d, soonest "
         "due first. #at/town/bank is on the Town run (its card says \u201cbank\u201d)."},
        {"Today", "", "Today tab › Logbook", "What got done, newest first, by day",
         "Ticked todos, and projects Completed or Dropped, with the time. Untick one there "
         "and it goes back."},
        // ── Timeline (s059) ─────────────────────────────────────────────────
        {"Today", "win.timeline", "", "The timeline: everything along the days",
         "A running calendar on top, each day's projects, todos and notes hanging under it -- "
         "crowded days stack down. Click picks, double-click opens, right-click opens a clump "
         "(a project's steps). Week / Month / Season (the three icons); drag or scroll to move, "
         "Ctrl+scroll zooms; drag the box in the corner map. Group ▾: Day / Place (a lane per "
         "#at/ place) / Purpose (a lane per goal or project). Show (the funnel): projects, todos, "
         "notes, Someday, links. Its find takes Find's words, #tag, is:, due: -- "
         "Enter / Shift+Enter step through. Someday brings in undated work. Links draws a curve "
         "between two things that link to each other (point at one to light its links). Also "
         "the fourth button beside Source / Live / Reading. It opens as you left it: zoom, Group "
         "and Show are kept."},
        {"Today", "", "Drag a line to a day", "On the timeline: re-date a todo or project by dragging it",
         "Its due moves (the time of day kept), or its start if it only starts there; a Someday "
         "one gets a due. The day lights up in the strip. Grouped by Place or Purpose, drop it in "
         "another lane to change its place or what it feeds; drop it in Someday to take its date "
         "away. Esc puts it back; Ctrl+Z undoes. Notes and done work stay on their day."},
        // ── Glance at Today (s066) ──────────────────────────────────────────
        {"Today", "win.glance", "", "The Glance at Today: the day on one card, to take away",
         "Late, due today, running short, starting today, flagged, then errands by place -- each "
         "thing once. Copy (Keep, Notes, a message), Email (to the address at the foot; Gmail "
         "works as the browser's mail), Save .ics (Google Calendar \u203a Import, or attach it to "
         "a mail and the iPhone offers Add to Calendar). Drag the .ics chip at the foot onto a mail "
         "you are writing to attach it. Click a line to show its note. Also the card-and-sun "
         "button in the header, and the top of the main menu."},
        // ── Forecast (s039) ─────────────────────────────────────────────────
        {"Today", "", "Today tab › Forecast", "The days ahead: what is due, and what starts, day by day",
         "A strip of today and the next six days with a count on each, then Later. Pick a day. "
         "Overdue stays pinned on top."},
        {"Today", "", "Due notification", "A desktop notice when a todo comes due",
         "Click it to open the todo. On in Preferences; jot can keep running with "
         "its window closed so the clock keeps ticking."},
        // ── Notification actions (s041) ─────────────────────────────────────
        {"Today", "", "Notification › Mark done", "Tick a due todo from the tray",
         "jot does not come forward. A notice older than the todo's due date (moved, or a "
         "repeat that rolled on) changes nothing -- the Today footer says why."},
        {"Today", "", "Notification › In 1 hour / Tomorrow", "Snooze a due notice",
         "It is said again then. Only the notice waits: the todo keeps its dates and stays "
         "on Today."},
        {"Today", "", "Calendar", "Dated todos in GNOME's top-bar calendar",
         "Today pane › Show dated todos on the desktop. One way: jot writes, the "
         "calendar only shows."},
        // ── Tags (s035) ─────────────────────────────────────────────────────
        {"Tags", "win.show-tags", "Tags tab", "Pick a #tag: its todos you can do, the ones waiting, its notes",
         "From all over the tree. Press the chip again to let it go. Done ones are folded at the foot."},
        {"Tags", "", "Find a tag", "Type over the chips to narrow them; Enter picks the one that matches",
         "ho finds #home and #phone. Esc clears. Ctrl+Shift+T again (or with nothing to pick) lands in it."},
        {"Tags", "", "Note details › Tags", "Add a tag: type it and press Enter, or pick from ▾",
         "The same list as the chips over the note. × takes it off. Ctrl+Z undoes either."},
        {"Tags", "", "Click a tag", "Jump to that tag's list",
         "A chip in note details, Ctrl+click a #tag in the note, or a plain click in Reading."},
        {"Tags", "", "The chips over a note", "A note's tags: click one for its list, × to take it off, + to add",
         "They are kept out of the text (front matter, in the file)."},
        {"Tags", "", "#word in the text", "Type a tag where you are writing",
         "It moves up to the chips when you leave the note. \\#word stays as text."},
        {"Tags", "", "#home/garden", "Nest a tag with a slash",
         "Picking #home shows #home/garden too. #Home and #home are the same tag."},
        {"Tags", "", "Contexts", "Use tags as places or moods: #errands, #calls, #low-energy",
         "A tag on a project is not passed to its steps -- tag the steps you do there."},


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
        // ── Graph (s071f) ───────────────────────────────────────────────────
        {"Views", "win.graph", "", "The graph: every note a bubble, the lines between them",
         "Links, the tree and (in Show) shared tags are the lines; todos wear their state's colour. "
         "Click picks, double-click opens, drag a bubble to pin it; scroll or drag the space to move, "
         "Ctrl+scroll, Ctrl+= / Ctrl+- or + / - to zoom, Ctrl+0 to see it all; the map in the corner shows where you are -- press or drag in it. Right-click a bubble: Center Here gathers what is within two lines of it; Show "
         "Everything goes back. Group pulls tags, places or projects into clumps. Also the fifth "
         "button beside Source / Live / Reading / Timeline."},
        {"Views", "", "Header, top right: </> · pen · book",
         "Source, Live Preview, Reading: the pressed one is the view you are in",
         "One click to any of the three. The keys below still work."},
        {"Views", "win.toggle-reading", "", "Reading: the note without its marks",
         "Double-click anywhere to go back to editing at that spot."},
        {"Views", "win.toggle-live", "", "Live Preview: marks show only where the cursor is",
         "Off: every mark on screen (plain Source)."},
        {"Views", "win.zoom-in", "", "Bigger text in the note",
         "Every view; headings and code grow with it. Also Ctrl+scroll over the note, "
         "\u22ee \u203a View, or right-click in the note. Kept across restarts."},
        {"Views", "win.zoom-out", "", "Smaller text in the note",
         "A chip in the note's corner says the size whenever it is not 100%."},
        {"Views", "win.zoom-reset", "", "Back to 100%", "Or click the chip in the note's corner."},
        {"Views", "win.toggle-tree", "", "Show or hide the side pane",
         "Or the top-left pair: the button with its LEFT side filled."},
        {"Views", "win.toggle-drawer", "", "Show or hide note details",
         "Or the pair's button with its RIGHT side filled. Both off: the note alone on screen."},

        // ── Command line ────────────────────────────────────────────────────
        {"Command line", "", "jot NAME -l milk eggs \"rye bread\"",
         "Add task lines to the note NAME", "Made if there is no note of that name."},
        {"Command line", "", "jot NAME -a some words", "Add a line of text to the note NAME",
         "The reply says Made or Added, so a mistyped name shows."},
        {"Command line", "", "jot NAME -al \"a line\" milk eggs",
         "A line of text, then todos under it, in one go",
         "Quote the line; \"\" for no line. Or jot NAME -a some words -l milk eggs -- each flag takes the "
         "words after it. The Jot tile takes both too."},
        {"Command line", "", "jot --capture", "Open jot on the capture line", ""},
        {"Command line", "", "jot --glance", "Print the Glance at Today and exit",
         "jot --glance ics prints the day as a calendar file. Works with jot closed -- a cron job "
         "at 7:00 can mail it to you."},
        {"Command line", "", "jot --background", "Start jot with no window, running in the background",
         "What Start at login runs (./install.sh asks). With jot already running it does nothing."},
        {"Command line", "", "./install.sh  /  ./uninstall.sh", "Install jot for your login, or take it all out",
         "In the source folder. install.sh: a release build into ~/.local/bin, the launcher, the icon, the "
         "Jot tile, Start at login if you say yes; run it again to update. uninstall.sh never touches a "
         "jots folder, and asks before taking your settings."},
        {"Command line", "", "JOT_DEBUG=info:all jot", "A talkative console, for chasing a problem",
         "Quiet (warnings only) without it."},

        // ── Jots folders ────────────────────────────────────────────────────
        {"Jots folders", "", "Note menu \u203a Export\u2026", "Export the selected notes as markdown, each one file",
         "One note: name the file (its title to start). Several: pick a folder, each named for its note. "
         "Each top-level note is one file: its facts in front matter, the notes under it "
         "as headings, todos as boxes with their dates on the line. Pictures go too. Also the tree's "
         "right-click."},
        {"Jots folders", "", "Main menu \u203a Export All\u2026", "Every note out as markdown, into a new folder you name",
         "One file per top-level note, an attachments folder beside them, links between notes kept."},
        {"Jots folders", "win.new-jots", "", "Start a new jots folder", ""},
        {"Jots folders", "win.open-jots", "", "Open a jots folder",
         "Main menu › Recent jots for the ones you have used."},
        {"Jots folders", "win.close-jots", "", "Close the jots folder",
         "Everything is saved first. jot then has no folder open, and starts that way next time too."},
        {"Jots folders", "win.save-all", "", "Save now",
         "jot saves by itself; this just does it this instant."},
        {"Jots folders", "win.save-as", "", "Copy everything to a new jots folder and switch to it",
         ""},
        {"Jots folders", "", "The folder name in the header",
         "Open in Files, Copy path, Rename or Relocate the jots folder", ""},

        // ── Help ────────────────────────────────────────────────────────────
        {"Help", "win.help", "", "jot Help: a page for each part of jot -- what it is for, how to use it",
         "Each page has Try it buttons that open the thing it is about. Main menu › jot Help."},
        {"Help", "", "The ? in a view", "Its page in the guide",
         "Beside the side pane's tabs (the page for the tab on show), at the top right of the "
         "timeline, at the top left of the Glance."},
        {"Help", "", "jot Help › Writing in markdown", "Make the markdown tour a note",
         "Every mark in one note -- read it in Source, then in Reading. An ordinary note: "
         "Ctrl+Z takes it back."},
        {"Help", "win.cheat-sheet", "", "This cheat sheet — type to find a line",
         "The Cheat Sheet page of the Help window; a guide page's “Every key for this” lands here filtered."},
        {"Help", "win.shortcuts", "", "Keyboard Shortcuts: every key, drawn as keycaps, a card per section",
         "Type words or keys to find one -- \u201cflag\u201d, \u201cctrl+m\u201d; the keys found light up. Main menu \u203a Keyboard shortcuts."},
        {"Help", "", "Keyboard Shortcuts \u203a Edit", "Change any shortcut: click it, press the new key",
         "A key another verb has asks first (Use It Here takes it from there). Backspace: no key. \u21ba puts "
         "back one; Preferences \u203a Keyboard \u203a Reset All to Defaults puts back all. Menus, this sheet "
         "and the guide show your keys."},
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
