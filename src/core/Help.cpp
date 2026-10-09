#include "core/Help.hpp"
#include "core/CheatSheet.hpp"
#include "core/Shortcuts.hpp"

#include <algorithm>
#include <cctype>
#include <set>

// Help.cpp -- the guide's pages. See the header for what it is for and the
// stones the selftest holds it to.
//
// House style for a page: the LEAD is the idea in one sentence. The body says
// why it is built that way and how to use it, in short paragraphs -- a reader
// who stops after the first one should still have the idea. Keys are named
// sparingly (the cheat sheet has them all; "Every key for this" goes there),
// and a Try only names a verb that always does something, whatever is selected.
// When a milestone changes what a part of jot is FOR, its page changes in the
// same ship.

namespace jot::core {

namespace {

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

const ShortcutSpec* spec_for(const std::string& action) {
    for (const auto& s : shortcut_registry())
        if (s.action == action) return &s;
    return nullptr;
}

}  // namespace

const std::vector<std::string>& help_groups() {
    static const std::vector<std::string> kGroups = {
        "Start here", "Getting it down", "Getting it done", "What finished means",
        "Seeing it", "Around jot",
    };
    return kGroups;
}

const std::vector<HelpTopic>& help_topics() {
    // Fields: {id, group, title, icon, lead, body, tries, keys, see}
    static const std::vector<HelpTopic> kTopics = {
        // ── Start here ──────────────────────────────────────────────────────
        {"welcome", "Start here", "Welcome to jot", "jot-app-logo-symbolic",
         "jot is a scribble pad that files itself: write first, organise after.",
         {
             "Most task apps ask where a thing goes before you have written it down. "
             "jot asks afterwards. You open it and type; the filing is a second act, "
             "done when you have a minute for it.",
             "Everything in jot is a **note** — markdown text with a name. A note "
             "can hold other notes, so the whole of it is one tree. A **todo** is a note "
             "with a tick; a **project** is a note with todos under it. Nothing has to "
             "become something else to change its job.",
             "Then jot goes a step further than a list. A piece of work can say what "
             "*finished* means — the documents it needs, the date it is due, the "
             "place you do it, the small steps that feed it — and jot keeps track "
             "of how far along it is.",
             "A good first ten minutes:",
             "- Capture three things that are on your mind (**Getting it down**).",
             "- Make one of them a todo and give it a due date (**Todos**).",
             "- Look at **Today** to see what jot thinks you can do now.",
             "- Open the **timeline** to see the days ahead.",
             "Use the list on the left, or **Next** at the foot of each page, to read on.",
         },
         {{"Capture a thought", "win.capture"}},
         "",
         {"capture", "todos", "done-when"}},

        // ── Getting it down ─────────────────────────────────────────────────
        {"capture", "Getting it down", "Capture", "list-add-symbolic",
         "Get a thought out of your head in a second, from wherever you are.",
         {
             "A thought you have to walk across the room to write down is a thought "
             "you lose. So jot has many doors, and every one of them lands in the same "
             "place: a new note in the **Inbox**, with nothing else moved.",
             "- **The capture line** in jot's header. Type, press Enter, type the next. "
             "The cursor stays put.",
             "- **Your own key**, anywhere on the desktop, jot open or not. Set it in "
             "Preferences › Capture.",
             "- **The Jot tile** in GNOME's quick settings: its › opens a capture "
             "line. `./install.sh` puts it in with the rest of jot.",
             "- **The command line**: `jot --capture \"ring the vet\"`. With jot closed, "
             "the thought is kept and filed the next time jot opens.",
             "Words with a # become tags as they arrive: \"ring the vet #pets\" files "
             "**ring the vet**, tagged pets.",
             "The tile and the command line also add to a named note. "
             "`jot Groceries -l milk eggs` puts two todos in Groceries (making it if "
             "need be); `jot Groceries -a check the pantry` adds a line of text.",
             "Do not sort while you capture. That is what the Inbox is for.",
         },
         {{"Go to the capture line", "win.capture"}},
         "capture",
         {"inbox", "outside"}},

        {"inbox", "Getting it down", "The Inbox", "jot-tab-inbox-symbolic",
         "Everything you captured and have not decided about yet.",
         {
             "The Inbox is a **mark**, not a place. A captured note sits at the top of "
             "the tree like any other; the Inbox tab simply lists the notes that still "
             "carry the mark, and its tab counts them.",
             "Empty it when you have a few minutes. For each note, decide what it is:",
             "- **It belongs somewhere** — Move it: type where it goes and press "
             "Enter. Your recent places come first.",
             "- **It is something to do** — make it a todo, give it a date.",
             "- **It is fine where it is** — Keep takes it off the list.",
             "- **It is done already** — tick it.",
             "A note you have dealt with stays in the list, dimmed, so you can see what "
             "you did. **Clean Up** clears those rows off the Inbox. Nothing is moved or "
             "deleted by it.",
             "Any note can be put back on the Inbox by hand from its ⋮ menu — "
             "a way to say \"come back to this\".",
         },
         {{"Open the Inbox", "win.left-view::inbox"}},
         "inbox",
         {"capture", "notes", "todos"}},

        {"notes", "Getting it down", "Notes and the tree", "jot-tab-notes-symbolic",
         "Any note can hold other notes, so all of it is one outline you can reshape.",
         {
             "There are no folders and no documents in jot — only notes. A note about "
             "the house can hold the notes about each room, and any of those can hold "
             "more. Drag a note onto another to put it inside; onto a row's top or bottom "
             "edge to put it beside.",
             "The tree is also an outliner. With the tree in focus, **Enter** makes a new "
             "note below, ready to name; **Tab** and **Shift+Tab** indent and outdent; "
             "**Alt+Up** and **Alt+Down** move a note among its neighbours. "
             "Ctrl+click and Shift+click select several, and Delete, Tick, Flag and Move "
             "then act on all of them.",
             "Notes link to each other. **Copy Link** on a note and paste it into "
             "another: the link follows its note through every rename and move, because "
             "it points at the note itself, not its name. Note details › Linked from "
             "shows what points here.",
             "**Note details** — the pane on the right — is everything about a "
             "note that is not its text: dates, tags, links, attached files. The two "
             "buttons at the top left show and hide the side pane and Note details.",
             "**Protect** a note to lock it against edits, moves and deletes.",
         },
         {{"Show the notes", "win.left-view::notes"}, {"New note", "win.new-note"}},
         "tree",
         {"writing", "inbox", "undo"}},

        {"writing", "Getting it down", "Writing in markdown", "jot-view-live-symbolic",
         "Plain text with a few marks, shown three ways.",
         {
             "A note's text is markdown: `**bold**`, `# Heading`, `- item`, "
             "`- [ ] a box to tick`. It is plain text underneath, so it reads anywhere "
             "and outlives any app.",
             "The three buttons at the top right choose how you see it:",
             "- **Source** shows every mark.",
             "- **Live Preview** hides the marks except where the cursor is — "
             "writing, but tidy.",
             "- **Reading** shows the note finished. Double-click to go back to "
             "writing at that spot.",
             "New to the marks? **Make the markdown tour a note**, below: every mark "
             "in one note, to read in Source and then in Reading.",
             "The **format bar** above the note writes the marks for you; every button "
             "toggles. Lists carry on when you press Enter, and Tab nests an item.",
             "Drop a **picture or any file** on the note and it lands where you let go "
             "— copied into the jots folder, so it travels with your notes. Hold "
             "Shift while dropping to link to the file instead. A screenshot pastes "
             "straight in with Ctrl+V.",
             "Too small to read? **Ctrl+=** makes the note's text bigger, in every view; "
             "**Ctrl+0** puts it back.",
             "A `- [ ]` line in a note is an informal box — a shopping list, a "
             "note to self. It ticks, but it is not a todo: todos are notes, and only "
             "they reach Today.",
         },
         {{"Make the markdown tour a note", "win.markdown-tour"},
          {"Live Preview on or off", "win.toggle-live"}, {"Reading view", "win.toggle-reading"}},
         "writing",
         {"notes", "packets"}},

        // ── Getting it done ─────────────────────────────────────────────────
        {"todos", "Getting it done", "Todos", "jot-view-available-symbolic",
         "A todo is a note with a tick — it keeps its text, its notes and its files.",
         {
             "Make any note a todo and it gains a tick, and Note details gains the "
             "fields a todo needs. It stays a note: the details of the job, the phone "
             "number, the receipt all live in it.",
             "- **Due** — when it must be done. jot warns you when it comes due.",
             "- **Defer** — when it can start. Until then it keeps out of your "
             "lists, so today is not cluttered with next month.",
             "- **Flag** — \"this one, today\". Flagged todos are on Today whatever "
             "their dates.",
             "- **Time** — how long it takes: 15m, 1h30. Find can then show what "
             "fits the half hour you have.",
             "- **Repeat** — weekly, every 2 weeks, monthly. Ticking it logs this "
             "time and moves its dates on.",
             "There is **no priority number** — within a month everything is a 1. "
             "The order of the list is the priority: drag the important one up.",
             "jot works out what you can do now. A todo that is deferred, that waits "
             "behind an earlier step, or whose project is on hold, is not **available**, "
             "and the Today views leave it out until it is.",
         },
         {{"Open Today", "win.left-view::today"}},
         "todos",
         {"projects", "today", "done-when"}},

        {"projects", "Getting it done", "Projects and review", "jot-tab-projects-symbolic",
         "A project is a note with steps under it, and it wants a look now and then.",
         {
             "Put todos under a note and it is a project; its todos are the steps. You "
             "can also say so on purpose — **This is a project** in Note details "
             "— or say a note never is one.",
             "How the steps run is the project's choice (Note details › Structure):",
             "- **Sequential** — one at a time; only the first step not done is "
             "offered.",
             "- **Parallel** — all of them at once.",
             "- **Single actions** — a list of loose todos, nothing first. Good for "
             "errands.",
             "A project can be **Active**, **On hold** (its steps leave your lists), "
             "**Completed** or **Dropped** (kept, out of every list). Its own due, defer "
             "and flag pass down to its steps.",
             "**Review** is how projects stay true. Each comes up for a look once a "
             "week (or as often as you say): is the next step right, is anything stuck, "
             "should it go on hold? The Projects tab lists the ones due a look; press "
             "✓ when it has had one and the next comes up.",
         },
         {{"Open Projects", "win.show-projects"}, {"New project", "win.new-project"}},
         "projects",
         {"todos", "done-when", "deadlines"}},

        {"today", "Getting it done", "Today, and the days ahead", "jot-tab-today-symbolic",
         "The views that answer \"what can I do now?\"",
         {
             "The Today tab is a report, not a list you keep. It reads your whole tree "
             "and shows what fits, grouped by the project each todo belongs to. Anything "
             "late is pinned on top.",
             "- **Today** — due by tonight, plus whatever is flagged.",
             "- **Available** — everything you could do now: not blocked, not "
             "deferred.",
             "- **Flagged** — everything flagged.",
             "- **Logbook** — what got done, by day. Untick one there to bring it "
             "back.",
             "- **Forecast** — today and the six days after: what is due and what "
             "starts on each.",
             "- **Errands** (the pin) — todos gathered by the place you do them.",
             "jot also speaks up. A **notification** comes when a todo falls due; its "
             "buttons tick it or snooze it without opening jot. jot can keep running "
             "with its window closed so the clock keeps ticking, and can put dated todos "
             "in GNOME's top-bar calendar — both in Preferences.",
         },
         {{"Open Today", "win.left-view::today"}, {"Glance at Today", "win.glance"}},
         "today",
         {"todos", "tags", "glance", "timeline"}},

        {"tags", "Getting it done", "Tags and places", "jot-tab-tags-symbolic",
         "A tag gathers work from all over the tree; a place turns it into a trip.",
         {
             "The tree says what a note is *part of*. A tag says what it is *like*: "
             "#calls, #low-energy, #waiting. A note can carry any number, and the "
             "Tags tab lists everything with a tag — what you can do, what is "
             "waiting, and the notes.",
             "A note's tags sit as **chips** over its text: click one for its list, "
             "× to take it off, + to add one. You can also type #tag as you write; "
             "it moves up to the chips when you leave the note. In the file they are "
             "kept in the note's front matter, out of the text.",
             "Tags nest with a slash: #home/garden is under #home.",
             "A **place** is a tag under #at/ — #at/town, #at/hardware-store. "
             "Every todo with a place joins that place's **errand run**: Today › "
             "Errands shows one heading per place, with what the trip adds up to "
             "(\"3 errands · ~25m · 1 due this week\"), so when you are in "
             "town you do everything that is in town.",
             "A tag on a project is not passed to its steps — tag the steps you do "
             "there.",
         },
         {{"Open Tags", "win.show-tags"}},
         "tag",
         {"today", "find", "timeline"}},

        // ── What finished means ─────────────────────────────────────────────
        {"done-when", "What finished means", "Done when", "jot-state-done-symbolic",
         "A piece of work says up front what finished means, and jot checks it.",
         {
             "Most lists close a project when the last box is ticked — and the "
             "boxes were only ever a guess. In jot a todo or project can say what "
             "*finished* means, and then \"done\" is a fact jot can see, not a feeling.",
             "Note details › **Done when**:",
             "- **Just the tick** — the usual. Done when you say so.",
             "- **Every item is in** — each `- [ ]` line in its text is ticked or "
             "has a file on it.",
             "- **Every step is done** — each todo directly under it is ticked.",
             "Cards and the tree then show the count (\"2 of 3 in\"). Tick it before it "
             "is met and jot asks first, naming what is missing — Tick Anyway if you "
             "mean it.",
             "On top of that sit jot's **strategies**: shapes of work it knows how to "
             "help with. A **packet** gathers documents and sends them once. A "
             "**deadline** works back from its date. An **errand run** bunches work by "
             "place. A **routine** repeats and keeps its record, and **feeders** keep "
             "a goal on pace. The next pages take them one at a time.",
         },
         {},
         "done when",
         {"packets", "deadlines", "routines", "tags"}},

        {"packets", "What finished means", "Packets", "mail-attachment-symbolic",
         "The things a piece of work needs, gathered as they turn up and sent in one go.",
         {
             "Taxes, a passport renewal, a claim: each needs a set of documents, and they "
             "turn up one at a time over weeks. A packet keeps them in one place.",
             "Make a note a packet (Note details › Done when › **A packet**) and "
             "write each thing it needs as a `- [ ]` line. An item is **in** when you "
             "drop its file onto its line — or tick it, for a paper copy. Note "
             "details says \"3 of 5 in · missing: ...\".",
             "- **Nudge me** reminds you, every day or week or month, what is still "
             "missing — a notification after 9:00, with I've got it to tick the "
             "last one.",
             "- **Gather for sending**, once everything is in, copies every file into "
             "one folder or one zip, each named for its item, with a contents list. The "
             "packet is stamped Sent.",
             "- **Do it again** makes next year's copy: the same items, nothing in, due "
             "a year on, with a link back to last time. The old one stays as the record. "
             "Its routines (\"scan receipts\", every week) feed the new one from then on.",
         },
         {},
         "packet",
         {"done-when", "writing", "deadlines"}},

        {"deadlines", "What finished means", "Deadlines", "jot-view-forecast-symbolic",
         "Work back from the due date, so the last week is not the only week.",
         {
             "Give a project a due date and choose Note details › Done when › "
             "**A deadline**. jot counts what is left — the steps not done, and "
             "their time estimates — against the days left: \"3 steps · ~2h "
             "· 9 days out\", then \"On track — start by Fri 11 Dec\".",
             "The rule is simple on purpose: a day for each step (or a day for each hour "
             "of work), plus one spare.",
             "When the days left come down to what the work needs, the next step that can "
             "start goes on **Today** under **Running short**. Tick it and the next one "
             "comes. The tree marks the project with ⏳ — orange when short, red "
             "when late.",
             "Nothing is moved or rescheduled for you. The deadline is a reading of where "
             "you stand, said early enough to act on.",
         },
         {{"Open Today", "win.left-view::today"}},
         "deadline",
         {"projects", "routines", "timeline"}},

        {"routines", "What finished means", "Routines and feeders", "view-refresh-symbolic",
         "A repeating todo keeps its record; small repeating steps can feed a goal.",
         {
             "A todo that repeats is a **routine**, and for a routine the history is the "
             "point: did the receipts get scanned every week, or did they slip? Each time "
             "you tick it, jot keeps the occurrence and whether it was on time.",
             "Projects › **Routines** lists every routine, slipped ones first "
             "(\"2 missed since Sun 20 Sep\", in red), each with its last ten as dots "
             "— green on time, orange late, a red ring for each missed.",
             "A **feeder** is a small repeating step that serves a bigger goal: \"scan "
             "receipts\" feeds \"Taxes 2027\". Set it in the step's Note details › "
             "Todo › **Feeds**. The goal then adds it up under Done when: "
             "\"Fed by 2 feeders · ~9h before the due · ~20m a week\" — "
             "in red when one has slipped — and jot sends a notification the morning a "
             "feeder slips (\"“scan receipts” has slipped\"), once until it is caught up.",
             "So a big job is done the easy way: a little, often, and you can see whether "
             "the little is keeping up.",
         },
         {{"Open Projects", "win.show-projects"}},
         "repeat",
         {"todos", "deadlines", "timeline"}},

        // ── Seeing it ───────────────────────────────────────────────────────
        {"find", "Seeing it", "Find and perspectives", "system-search-symbolic",
         "Ask the whole tree a question, and keep the questions you ask often.",
         {
             "Find (above the tree) looks for every word you type, in names and text. "
             "Click a result and the note opens with the word selected.",
             "It also takes a small query language:",
             "- `#errands #town` both tags, `#a or #b` either, `-#done` not.",
             "- `is:available` `is:waiting` `is:flagged` `is:project` `is:inbox`.",
             "- `due:today` `due:week` `due:overdue` `due:none`.",
             "- `est:30` — todos that take half an hour or less.",
             "You need not remember any of it: the **funnel** beside Find writes the "
             "words for you. A query you use often can be saved as a **perspective** "
             "— \"available, 30 minutes, #calls\" — and opened again from the "
             "funnel.",
         },
         {{"Find", "win.find"}, {"Perspectives", "win.perspectives"}},
         "find",
         {"tags", "timeline"}},

        {"timeline", "Seeing it", "The timeline", "jot-view-timeline-symbolic",
         "Your work laid along the days, so you can see the shape of the weeks ahead.",
         {
             "The timeline takes the note's place: a running calendar along the top, and "
             "each day's projects, todos and notes hanging under it in a card. Crowded "
             "stretches stack downward. Late work is red, today's orange, done green.",
             "- **Right-click** a card to open it: a project's steps come out.",
             "- **Drag a line to another day** to re-date it. Esc puts it back; Ctrl+Z "
             "undoes. Grouped by **Place**, drop it in another lane and its place changes; "
             "by **Purpose**, it feeds that lane's goal. Drop it in **Someday** (turn it on "
             "in Show) and its date goes.",
             "- **Week, Month, Season** — the three icons — zoom out to see "
             "further.",
             "- **Group** lays the cards in lanes: by **place** (one lane per #at/ "
             "place) or by **purpose** (the goal or project each thing serves).",
             "- **Show** (the funnel) brings in **Someday** — undated work, past "
             "the last day — and the curves between things that link to each other.",
             "- Type to **find**: matches stay bright, the rest dim; Enter steps "
             "through.",
             "The map in the corner shows the whole span; drag its box to go there. "
             "Esc, or any of Source / Live / Reading, brings the note back.",
         },
         {{"Show the timeline", "win.timeline"}},
         "timeline",
         {"today", "deadlines", "tags"}},

        {"glance", "Seeing it", "Glance at Today", "jot-glance-symbolic",
         "The day on one card, to carry away from the desk.",
         {
             "Open it with the card-and-sun button at the top right of jot, the top of "
             "the main menu, or **Ctrl+Shift+G**.",
             "The Glance is the day in the order it matters: what is late, what is due "
             "today, what is running short, what starts today, what is flagged, then "
             "errands by place. Each thing once.",
             "It is a card to take with you, so it has three ways out:",
             "- **Copy** — paste it into Keep, Notes or a message.",
             "- **Email** — a new mail with the day in it, to the address at the "
             "foot.",
             "- **Save .ics** — a calendar file: an all-day card with the list, and "
             "a slot for each thing due at an hour. Google Calendar imports it; the "
             "iPhone's Mail offers to add it.",
             "A mail link cannot carry an attachment, so the foot holds the day's .ics "
             "as a chip: drag it onto the mail you are writing.",
             "Click any line to show its note.",
             "Want it without opening jot? `jot --glance` prints the day in a terminal "
             "(`jot --glance ics` as a calendar file) — jot closed or not, so a cron job "
             "at 7:00 can mail it to you.",
         },
         {{"Open the Glance", "win.glance"}},
         "glance",
         {"today", "timeline"}},

        {"graph", "Seeing it", "The graph", "jot-view-graph-symbolic",
         "Every note a bubble, and the lines between them.",
         {
             "The graph draws your whole jots folder at once: each note a bubble, "
             "bigger the more lines meet it. The lines are your **links** "
             "(`[label](jot:...)`), the **tree** (a note to the notes under it) and, "
             "if you ask in Show, **shared tags**. Todos wear their state's colour — "
             "late red, today orange, flagged amber, done green — like the cards.",
             "Bubbles push apart and lines pull together, so what belongs together "
             "gathers by itself.",
             "- **Click** a bubble to pick it (Note details follows); **double-click** "
             "to open the note.",
             "- **Drag** a bubble to put it somewhere — it stays (pinned) until you "
             "unpin it. Scroll, or drag the space, to move around; **Ctrl+scroll** "
             "or **Ctrl+=** / **Ctrl+-** zooms; **Ctrl+0** shows it all. When some of it is off screen, the **map** in the corner "
             "shows it all — press or drag in it to go there.",
             "- **Right-click** a bubble: **Center Here** gathers the graph to it — what "
             "is within two lines of it, the rest faded. Center Here on another to walk "
             "on; **Show Everything** (or Esc) goes back.",
             "- **Group** pulls each tag, place or project into its own clump, with a "
             "halo and its name: what goes together, at a glance.",
             "- The find field lights the bubbles that match: words, #tag, is:, due:.",
         },
         {{"Show the graph", "win.graph"}},
         "graph",
         {"timeline", "tags", "notes"}},

        // ── Around jot ──────────────────────────────────────────────────────
        {"outside", "Around jot", "jot outside its window", "utilities-terminal-symbolic",
         "Capture, notices and lists reach jot without opening it.",
         {
             "jot is meant to be in reach, not in the way. Most of what you do day to "
             "day can happen outside its window:",
             "- **The global capture key** (Preferences › Capture) and the **Jot "
             "tile** in quick settings file a thought into the Inbox.",
             "- **Notifications** say when a todo is due; Mark done and the snooze "
             "buttons work from the notice.",
             "- **The GNOME calendar** in the top bar can show your dated todos.",
             "- **Keep running** with the window closed (Preferences), so notices still "
             "come.",
             "The command line speaks the same language as the tile:",
             "- `jot --capture \"text\"` — a note in the Inbox.",
             "- `jot NAME -l milk eggs` — todos in the note NAME.",
             "- `jot NAME -a some words` — a line of text in it.",
             "- `jot NAME -al \"a line\" milk eggs` — both at once.",
             "- `jot --glance` — print the Glance at Today.",
             "With jot closed, all of these are kept and filed when it next opens. "
             "`jot --help` lists them.",
         },
         {{"Preferences", "win.preferences"}},
         "command line",
         {"capture", "today"}},

        {"folders", "Around jot", "Jots folders", "folder-symbolic",
         "Your notes live in a folder you choose, as plain files.",
         {
             "jot has no hidden database. A **jots folder** — named like "
             "`Home.jots` — holds a markdown file for each note, an `attachments` "
             "folder for the files you dropped in, and `jot.json`, which keeps the tree "
             "and the todo fields.",
             "jot starts with no folder at all. You can write straight away; the notes "
             "wait until you save them into a folder (Save As) or open one.",
             "- **Open** (Ctrl+O) and **New** (Ctrl+Shift+O) from the main menu; "
             "**Recent jots** lists the ones you have used.",
             "- **Close jots** leaves jot with no folder open, and it starts that way "
             "next time.",
             "- **The folder's name in the header** opens it in Files, copies its path, "
             "renames it or moves it.",
             "- **Import** brings markdown files, or a whole folder of them, in as notes.",
             "- **Export** takes them out as markdown anyone can read — Obsidian, a text "
             "editor, GitHub. Each top-level note is one file named for it: its facts at "
             "the top, the notes under it as headings, its todos as boxes with their "
             "dates on the line, like an outline. **Export…** in a note's menu does the "
             "selected notes (one note: you name the file, its title to start); "
             "**Export All…** in the main menu does everything, into a new folder you name.",
             "jot's own files are for jot — named by number, the tree kept in `jot.json`. "
             "Export is the copy for everything else.",
             "jot saves as you go; Ctrl+S only makes it this instant.",
             "- **Backups** happen by themselves: a copy of the whole folder for each "
             "day, refreshed as you work and when jot quits. Days where a file did not "
             "change share it, so a day costs only what changed. The last 7 days are "
             "kept, and one for each of the 4 weeks before. **Restore from Backup…** in "
             "the main menu brings a day back as a new folder beside this one — what you "
             "have now is never touched. Where they go, and when the last one was: "
             "Preferences › Backups. (They need rsync.)",
         },
         {{"Open a jots folder", "win.open-jots"}, {"Import markdown", "win.import-md"},
          {"Export all", "win.export-all"}, {"Back up now", "win.backup-now"}},
         "jots folder",
         {"notes", "undo"}},

        {"undo", "Around jot", "Nothing is final", "edit-undo-symbolic",
         "Every change but typing is one Ctrl+Z.",
         {
             "Try things. A move, a delete, a tick, a date, a tag, a whole import, a "
             "capture from the tile — each is one step, and **Ctrl+Z** takes it back. "
             "A deleted note comes back whole, with everything under it, selected. "
             "**Ctrl+Shift+Z** does it again.",
             "This works anywhere but a text box. In the note's text, Ctrl+Z is the "
             "text's own undo, one word or one format at a time. The main menu has Undo "
             "and Redo too.",
             "When jot asks before doing something — ticking a packet that is not "
             "all in, moving tags out of older notes — it is not because the change "
             "is final. It is because it might not be what you meant.",
         },
         {},
         "undo",
         {"notes", "folders"}},
    };
    return kTopics;
}

const HelpTopic* help_topic(const std::string& id) {
    for (const auto& t : help_topics())
        if (t.id == id) return &t;
    return nullptr;
}

int help_index(const std::string& id) {
    const auto& ts = help_topics();
    for (std::size_t i = 0; i < ts.size(); ++i)
        if (ts[i].id == id) return static_cast<int>(i);
    return -1;
}

bool help_matches(const HelpTopic& t, const std::string& query) {
    const auto ws = words(query);
    if (ws.empty()) return true;
    std::string hay = t.group + "\n" + t.title + "\n" + t.lead;
    for (const auto& p : t.body) hay += "\n" + p;
    hay = lower(hay);
    return std::all_of(ws.begin(), ws.end(),
                       [&](const std::string& w) { return hay.find(w) != std::string::npos; });
}

std::string help_markup(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 16);
    bool bold = false, ital = false, code = false;
    auto esc = [&](char c) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '\'': out += "&#39;"; break;
            case '"':  out += "&quot;"; break;
            default:   out += c;
        }
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '`') {
            out += code ? "</tt>" : "<tt>";
            code = !code;
            continue;
        }
        if (code) { esc(c); continue; }
        if (c == '*' && i + 1 < text.size() && text[i + 1] == '*') {
            out += bold ? "</b>" : "<b>";
            bold = !bold;
            ++i;
            continue;
        }
        if (c == '*') {
            out += ital ? "</i>" : "<i>";
            ital = !ital;
            continue;
        }
        esc(c);
    }
    // Close whatever is open, innermost first -- the result must parse.
    if (code) out += "</tt>";
    if (ital) out += "</i>";
    if (bold) out += "</b>";
    return out;
}

void help_split_action(const std::string& detailed, std::string& name, std::string& target) {
    const auto p = detailed.find("::");
    if (p == std::string::npos) {
        name = detailed;
        target.clear();
        return;
    }
    name = detailed.substr(0, p);
    target = detailed.substr(p + 2);
}

std::string help_try_keys(const HelpTry& t) {
    std::string name, target;
    help_split_action(t.action, name, target);
    if (const ShortcutSpec* s = spec_for(name)) return s->display_keys();
    return "";
}

namespace {
// view -> page. One table, so the "?" buttons and the selftest read the same.
const std::vector<std::pair<std::string, std::string>>& view_pages() {
    static const std::vector<std::pair<std::string, std::string>> kMap = {
        {"notes", "notes"},       {"find", "find"},         {"inbox", "inbox"},
        {"today", "today"},       {"available", "todos"},   {"flagged", "todos"},
        {"logbook", "today"},     {"forecast", "today"},    {"errands", "tags"},
        {"tags", "tags"},         {"projects", "projects"}, {"timeline", "timeline"},
        {"glance", "glance"},       {"graph", "graph"},
    };
    return kMap;
}
}  // namespace

std::string help_page_for(const std::string& view) {
    for (const auto& [v, page] : view_pages())
        if (v == view) return page;
    return "welcome";
}

const std::vector<std::string>& help_views() {
    static const std::vector<std::string> kViews = [] {
        std::vector<std::string> v;
        for (const auto& [name, page] : view_pages()) v.push_back(name);
        return v;
    }();
    return kViews;
}

const std::vector<std::string>& help_bound_actions() {
    // Bound in Shell_bindings.cpp with no key of their own. The tab buttons and
    // the View menu use left-view; a Try may too. s071: markdown-tour makes the
    // tour a note (the Writing page's Try).
    static const std::vector<std::string> kBound = {"win.left-view", "win.markdown-tour",
                                                     "win.export-all"};
    return kBound;
}

std::vector<std::string> help_unknown_actions() {
    std::vector<std::string> out;
    const auto& bound = help_bound_actions();
    for (const auto& t : help_topics())
        for (const auto& tr : t.tries) {
            std::string name, target;
            help_split_action(tr.action, name, target);
            const bool known = spec_for(name) ||
                               std::find(bound.begin(), bound.end(), name) != bound.end();
            if (!known) out.push_back(t.id + ": " + tr.action);
        }
    return out;
}

std::vector<std::string> help_bad_links() {
    std::vector<std::string> out;
    for (const auto& t : help_topics())
        for (const auto& s : t.see)
            if (!help_topic(s) || s == t.id) out.push_back(t.id + " -> " + s);
    return out;
}

std::vector<std::string> help_empty_keys() {
    std::vector<std::string> out;
    for (const auto& t : help_topics()) {
        if (t.keys.empty()) continue;   // no "Every key for this" on that page
        bool any = false;
        for (const auto& l : cheat_sheet())
            if (cheat_matches(l, t.keys)) { any = true; break; }
        if (!any) out.push_back(t.id + ": " + t.keys);
    }
    return out;
}

}  // namespace jot::core
