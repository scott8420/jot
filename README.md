# jot

A scribble pad that files itself.

Open it and type into a note; the organising comes after. Notes are
markdown, arranged in a tree where any node can parent any other, with
images and media embedded inline in the markdown body. Tasks live
inside that markdown as checkbox lines and are gathered into
GTD-shaped views. Nodes link to each other by identity, so a link
survives a rename and a move.

GTK4 / gtkmm, C++23, Linux.

## Jots on disk

Your notes live in a folder — "jots" in the app's own language:

```
MyJots/
  jot.json           the project file: tree, sibling order, titles, flags
  notes/<uuid>.md    one file per note: front-matter id, then the body
  attachments/       media, referenced relatively from the markdown
```

The project file owns the structure, so **a drag writes `jot.json` and
nothing on disk moves** — no rename, no `mv` of a subtree, no stale
path. A note's file keeps its name for the life of the note.

Each file also carries its own `id` in front-matter. Not for
interchange — export is what produces human-readable files — but for
recovery: lose `jot.json` and every note comes back as a top-level
orphan instead of a folder of anonymous uuids.

Structure saves the instant it changes. Body text saves on a short
timer, on leaving a note, and on close; Ctrl+S forces it.

## Capture

**Type into the line in the header and press Enter.** A new unfiled note
exists and you have not left the one you were reading — no selection
moves, no pane changes, the cursor stays in the box for the next
thought. `Ctrl+Shift+Enter` jumps to it from anywhere.

A captured line never loses anything. The first line becomes the title;
anything after it becomes the body; and a single line too long for a
tree row is cut at a word boundary for the title with the whole of it
kept in the body.

### From outside jot

```sh
jot --capture "ring the vet about the booster"   # files it, silently
jot --capture                                    # opens the capture line
jot Groceries -l milk eggs "sourdough bread"     # tasks on the note "Groceries"
jot Groceries -a check the pantry first          # a line of text on it
```

The first plain word is the note's NAME; the flag says what to add.
`-l` (`--list`) adds one task per argument (`- [ ] milk`; quote an item
of several words). `-a` (`--append`) joins the words into one line of
text. If a note of that name exists (any case, anywhere in the tree, not
protected) it grows; otherwise a new note is made at the top level. It
answers in the terminal -- `Made "Groceries" with 3 items.`, `Added a
line to "Groceries".` -- so a mistyped name shows as "Made". With jot
closed it is filed the next time jot opens.

`./install.sh` puts `jot` in `~/.local/bin`, on your PATH (see Install).
The build alone does not.

jot is a **single instance**: with jot running, the second command
forwards its arguments to the running one over the session bus and
exits. The window is not brought forward — a capture from a script or a
keybinding should not land in front of whatever you were doing.

**With jot closed, the thought is spooled and jot still does not
open.** It is written into `~/.local/share/jot/pending/` and the next
launch files it, oldest first. Outside the jots folder on purpose:
`jot.json` with two writers is corruption, and a capture can happen
before any jots folder has been chosen.

A spool file holds the *only* copy of what you typed, so it is deleted
only once its note is written into a jots folder. jot with no folder
open leaves the spool alone and drains it the moment one appears. A
crash in the middle costs a duplicate, never a thought.

**There is no daemon** unless you ask for one: `./install.sh` offers
Start at login (`jot --background`, no window).

With text, the window is deliberately *not* raised: filing a thought
from a script or a keybinding should not throw a window in front of
whatever you are doing. Without text there is nothing to file, so the
only sensible reading is "let me type", and that does raise it.

### A tile in quick settings

```sh
./install.sh                # puts it in with the rest; log out and back in once
```

puts a **Jot** tile in GNOME's quick settings panel (top-right). Its ›
opens a line with the cursor in it: type, press **Enter**, and the
thought is in the Inbox; the line clears for the next one and says what
jot said ("Filed to the Inbox." or, with jot closed, "Filed. jot will
pick it up next time it opens."). **Open jot** sits under it.

The line takes the command line's three forms, and **What can I type?**
under it folds out to say so:

| Type | Gets |
|---|---|
| `ring the vet` | a note in the Inbox |
| `Groceries -l milk eggs "rye bread"` | todos in the note Groceries (made if new) |
| `Groceries -a check the pantry` | a line of text in Groceries |
| `Groceries -al "for Saturday" milk eggs` | a line, then todos under it, at once |
| `ring the vet #pets` | a note called "ring the vet", tagged #pets |

`-al` (or `-la`) takes the line first -- one word or a "quoted phrase" --
then the items. `Groceries -a for Saturday -l milk eggs` does the same, each
flag owning the words after it, in either order. One Ctrl+Z takes both back.
`Groceries -al "" milk eggs` -- an empty line -- is just the todos.

`#words` are tags in all three, the same on the command line: they leave the
text and join the note's tags -- `ring the vet #pets` is a note "ring the
vet" tagged pets; `Groceries -l milk rye #organic #errands` adds milk and rye,
and tags Groceries organic and errands.

## Tags

A note's tags sit over its text as chips -- **#errands ×** -- not in the
writing. Click one for everything tagged with it, × to take it off, **+** to
add one (Enter adds and stays open for the next; Esc folds it). Note details'
Tags field edits the same list.

Typing `#word` in the text still tags: when you leave the note the tag moves
up to the chips, one Ctrl+Z to put it back. `\#word`, `#42` and `C#` are not
tags and stay where they are.

In the file the tags are front matter, which Obsidian and friends read too:

```
---
id: 3f2a…
tags: [errands, town]
---
- [ ] milk
```

A folder with tags still in its notes' text (from before, or from another
app) is offered a one-time move when it opens -- one Ctrl+Z undoes it all.

The tile files nothing itself: every Enter is a `jot` command line,
run with the binary the desktop entry names. `./uninstall.sh` takes it out
(`./install-extension.sh` alone still works from a build tree).

### A global key for it

**Preferences → Capture → Global capture shortcut → Set…**, then press
the combination you want. jot writes it into GNOME's own custom
shortcuts, so it appears in **Settings → Keyboard → View and Customize
Shortcuts** alongside everything else you have set, and you can remove
it from either place.

The key runs `jot --capture` with no text: the capture line opens with
the cursor in it. **It works when jot is not running** — GNOME starts
it, and on a cold start the thought goes through the same path as any
other capture.

The row shows the exact command, which names *this* binary. Rebuild jot
somewhere else and the shortcut still points at the old path — set it
again from the new build and the row will say so.

A few rules the chord has to pass, and why:

- **Ctrl, Alt or Super has to be in it** (a function key on its own is
  fine). A global grab on a bare letter takes that letter everywhere on
  the desktop, including in the dialog you would use to undo it.
- **Esc, Return, Tab, Space, Backspace and Delete are refused**
  whatever is held down with them. The desktop needs them.

If another shortcut already claims the chord, jot says which one and
lets you decide — it can see GNOME's own bindings and your custom ones,
but not an extension's or another application's private grab, so
refusing on that evidence would be refusing on a half-answer.

**Not GNOME?** The row says the feature is unavailable and nothing else
changes: `jot --capture` still works from a terminal, a launcher, a
script, or a keybinding you set yourself.

*Why not the GlobalShortcuts portal:* it cannot fire when jot is not
running, and the application does not choose the chord — it proposes
one and the desktop asks the user, which is exactly the thing this
preferences row exists to do.

## The Inbox

Everything captured -- the header line, `jot --capture`, the global
shortcut, and a new note made by `jot --list` or `-a` -- lands in the
**Inbox** tab (Notes | Inbox | Today), whose label counts what is waiting.
Deal with each one: drag it under a project in the tree, tick it done, or
press **Keep** if it belongs where it is. Filed and done rows stay on the
list, dimmed, until **Clean Up** (the button, View > Clean Up Inbox, or
`Ctrl+Shift+K`) takes them off. Nothing moves or is deleted by Clean Up.
The note menu's **In Inbox** puts any note on the list by hand.

## Projects

Any note with notes under it is a project. **Note details > Structure**
says how its children run -- Sequential, Parallel, or **Single actions** (a
list of loose todos, all available) -- and where it stands: **Active**,
**On hold** (its todos leave Today and Available; a late or flagged one
still shows, marked On hold), **Completed** (everything inside counts as
done) or **Dropped** (kept, but out of every list and off the calendar). The
note menu's **Project** submenu sets the same thing. On a project that is
itself a todo, Completed is its tick.

## Export -- your notes as markdown anyone can read

jot's own files are named by number, with the tree and every date in
`jot.json` -- they are for jot. **Export** is the copy for everything else
(Obsidian, a text editor, GitHub):

- **Export…** in a note's ⋮ menu (or the tree's right-click) writes the
  selected notes. One note: a Save dialog, its title as the starting name --
  call it what you like. Several: pick a folder; each is named for its note.
  **Export All…** in the main menu writes every top-level note into a new
  folder you name ("Home export 2026-10-08" to start).
- Each top-level note is ONE file named for it, shaped like an outline: its
  facts in front matter (`todo`, `due`, `defer`, `flagged`, `estimate`,
  `repeat`, `project`, `tags`, `created`, `jot-id`), the notes under it as
  headings, its todos as boxes with their facts on the line --
  `- [ ] book the van — due Fri 9 Oct 17:00 · ⚑ · ~30m · #errands` -- and
  their text indented under them.
- Links between notes point at the file that holds the other note (or
  become plain words when it was not exported). Pictures and files are
  copied into an `attachments` folder beside, so their links still work.
- A name already taken in the folder is never overwritten: "Ideas 2.md".

## Help: the guide and the cheat sheet

**F1** or **Ctrl+Shift+H** (or main menu > jot Help) opens jot Help on its
**Guide**: a page for each part of jot -- capture, the Inbox, todos,
projects, Today, tags and places, done-when, packets, deadlines, routines
and feeders, Find, the timeline, the Glance, jots folders -- saying what it
is for and how to use it. Pages are listed down the left in reading order
(or press Next at the foot of each); "Find a page" narrows the list. Each
page has **Try it** buttons that open the thing it describes, and **Every
key for this** opens the cheat sheet filtered to that page.

A small round **?** opens the page for what you are looking at: beside the
side pane's tabs (the Inbox, Today's lens, Tags, Projects, or Find while it
has words), at the top right of the timeline, and at the foot of the Glance.
New to markdown? The Writing page's **Make the markdown tour a note** puts
every mark in one note (Ctrl+Z takes it back).

**Ctrl+H** opens the same window on its **Cheat Sheet**: everything jot
does, a line each -- the keys, the drags, the markdown marks, the views and
the command line. Type to narrow it -- "inbox", "due", "ctrl+m". Escape
clears a search; a second Escape closes the window. Keys shown in either
page are read from the same registry jot binds them from, so neither can
show a key jot does not answer to.

## Keyboard shortcuts -- and changing them

**Ctrl+?** (main menu > Keyboard shortcuts) shows every key as keycaps, a
card per section; type to search by words or by keys ("flag", "ctrl+m").
Press **Edit** to change one: click it, press the new key. A key another
verb already has asks first -- **Use It Here** takes it from there.
Backspace leaves a verb with no key; the ↺ on a changed row puts back
jot's own. **Preferences > Keyboard** counts the changes and has **Reset
All to Defaults**. jot refuses keys that typing needs (Ctrl+C, Ctrl+Z ...),
keys the note or the tree keep (Ctrl+B, Alt+Up ...) and plain letters.
The menus, the cheat sheet and the guide show your keys. Changes live in
prefs.json under `key_overrides`.

## The graph

**Ctrl+Shift+B** (or the fifth view button, joined to the timeline's)
puts the graph in the note's place: every note a bubble, bigger the more
lines meet it; lines for links between notes and for the tree (and, in
Show, dashed lines between notes that share a tag). Todos wear their
state's colour, projects a ring.

- Click a bubble to pick it (Note details follows); double-click to open
  the note. Hover lights it and its neighbours.
- Drag a bubble to put it somewhere -- it stays (pinned; right-click ›
  Unpin lets it go). Scroll (or drag the space) to move; Ctrl+scroll or
  Ctrl+= / Ctrl+- to zoom (jot's zoom keys -- they zoom the graph while it
  shows); Ctrl+0 or Home frames it all. When some of it is off screen, the map
  in the corner shows it all -- press or drag in it to go there.
- Right-click a bubble › **Center Here**: what is within two lines of it
  gathers, the rest fades. **Show Everything** (or Esc) goes back.
- **Group ▾** -- Tag, Place or Project -- pulls each into its own clump
  with a halo and its name. The funnel chooses which lines and bubbles.
- The find field lights the bubbles that match (words, #tag, is:, due:).

Group and Show are kept across restarts.

## Glance at Today

**Ctrl+Shift+G** -- or the card-and-sun button in the header, or the top
of the main menu -- opens the day on one card:
what is late, due today, running short on a deadline, starting today,
flagged, then errands by place -- each thing once. Click a line to show its
note. Three ways to take it with you, in the title bar:

- **Copy** -- the Glance as text (and formatted, for editors that take it).
  Paste it into Google Keep, Apple Notes on iCloud.com, or a message.
- **Email** -- a new mail with the Glance in it, to the address in the foot
  (remembered). With Gmail in the browser, set the browser as the mail
  handler.
- **Save .ics** -- a calendar file: one all-day event "jot — Wed 7 Oct" with
  the list in its notes, plus a slot for each thing due at an hour today.
  Import it in Google Calendar (Settings › Import), or attach it to a mail
  and the iPhone's Mail offers to add it to Calendar.
- **The .ics chip** at the foot is the same file, ready to drag: after
  Email, drag it onto the mail you are writing (Gmail in the browser too)
  to attach it. Click it to see the file in Files. On the iPhone, open the
  mail in Mail and tap the attachment -- **Add All**.

From a terminal, `jot --glance` prints the same card as text and
`jot --glance ics` prints the calendar file -- with jot running or not, and
with no display, so a cron job can mail it to you each morning:

```
# crontab -e   (7:00 every day; needs a working `mail`)
0 7 * * * /path/to/jot --glance | mail -s "jot today" you@gmail.com
```

It reads the last jots folder jot had open.

## Dated todos on the desktop

Tick **Show dated todos on the desktop** at the bottom of the Today
pane (or the same item in the menu) and every todo with a due date is
written into a local GNOME task list called `jot`, where the calendar
drop-down in the top bar picks it up: dots on the days, and the todos
listed under the day you click.

No GNOME Shell extension is involved, which is the point — an
extension would be a second codebase in GJS that breaks on most GNOME
releases.

**It is strictly one way.** jot owns the truth; the task list is a
projection jot wipes and rewrites. Ticking something off in the
calendar drop-down does nothing back to jot. Untick the box and jot
removes its rows and leaves the list behind; delete the list in
Evolution or GNOME Tasks and jot loses nothing.

What projects: a todo, not done, with a due date of its own or one
inherited from a parent. Blocked and deferred todos project too — the
calendar is a grid of days, and a due date is a fact about a day
whatever jot thinks of the task's readiness. The availability word
travels in the row's description rather than deciding whether the row
exists at all.

Undated todos never project. They have no day, and picking one for
them would put jot's opinion on the desktop's grid.

### Checking it without a desktop

`jot_desktop_probe` writes a fixture projection into EDS through the
real backend and then asks the list the same live query
`gnome-shell-calendar-server` asks. Run it against a throwaway data
directory, never your real one:

```
export XDG_DATA_HOME=/tmp/probe/data XDG_CONFIG_HOME=/tmp/probe/config
export XDG_CACHE_HOME=/tmp/probe/cache
mkdir -p /tmp/probe/data /tmp/probe/config /tmp/probe/cache
dbus-run-session -- ./build/jot_desktop_probe
```

It proves the mechanism, not the appearance.

## State

**The window remembers its size** and whether it was maximized, next to
the pane layout. **It does not remember its position**, and that one is
not coming: GTK4 removed window positioning and Wayland gives an
application no way to ask where it is or to place itself. The
compositor decides where; jot decides how big.

**Todos are notes (s007).** A node becomes a todo when you make it one;
it keeps its id, its body, its children, its links and its backlinks.
Due and defer dates, a flag, and a Sequential/Parallel setting on a
parent. **No priority field** — sibling order is the priority, and
dragging a step up the list is how you set it.

**Availability is computed, never stored.** A Sequential parent makes
only its first incomplete child available; a defer date hides a todo
until then; both dates inherit down the tree. The left pane's **Today**
view groups by parent and prints the first line of the parent's note
under the group header, because the parent's note is the *why*.

**Capture (s008, s013).** A capture line in the header and
`jot --capture`, single-instance, no daemon — and with jot closed, a
pending spool that the next launch drains. See above.

**Three panes (s006).** A tree on the left, the note in the middle, a
metadata drawer on the right, saved to a folder of jots.

The two side panes toggle with F9 and F10, and **both off is the
point**: the note alone on screen is the front door this app is built
around — capture first, file second. The layout you leave is the layout
you come back to.

The drawer carries what the note points at, what points back at it, its
tags, where it sits in the tree, its file, and — folded away, because
nobody recognises a note by `019f3e82-841e-77bc` — its id. Backlinks
come from an index built in one pass at load and kept current as you
type. "Copy link to this note" puts `[Title](jot:<id>)` on the
clipboard, which is what anyone wanting the id actually wanted.

Tags (`#like-this`) are shown and styled but **not yet editable** —
where GTD state lives is still an open decision, and an editor would be
a commitment to an answer.

**Persisted since s003.**

s002 built the same surfaces on hardcoded fixtures behind
`core::NodeSource`; s003 implemented that interface against the disk
and swapped it in. **`TreePane` and `EditorPane` did not change by a
line** — which was the test, and it passed.

Dropping onto a row's middle makes the dragged note a child; onto its
top or bottom edge, a sibling above or below. A move is a `parent_id`
write plus a sibling index, made by the model, never by the drop
handler. That is the defect Notr had and the reason drag
landed in the first milestone rather than a later one.

`JOT_STRESS=5000 JOT_DEBUG=info:tree ./build/jot` loads a synthetic set
instead of the fixtures, and every tree rebuild logs its row count and
milliseconds to the `tree` log area. That is the D6 instrument.

The console is quiet by default: warnings and errors only. `JOT_DEBUG`
turns the narration on -- `info:all` for what jot used to print by
default, `drawer,tree` for those areas at DEBUG, `all` for every area at
DEBUG, `trace:<area>` for one louder. The instrumentation is always in the
binary; this is the switch.

What the Cairn spine underneath carries: mandatory widget naming with a live registry,
a split class with the header as its index, per-area runtime-toggleable
logging, Gio actions including parameterised ones, a JSON persistence
pump with its encode/decode adjacent, forward/inverse text mapping with
a round-trip test, the compiled-in resource pipeline, hide-on-close
dialog lifetime, desktop light/dark following, and a shortcut registry
with one source and two consumers.

## Install

```sh
./install.sh        # build, check, install for your login -- no sudo
./uninstall.sh      # all of it back out; never a jots folder
```

`install.sh` makes a release build in `build-release/` (your `build/` is
left alone), runs the selftest, and installs only if it passes:
`~/.local/bin/jot`, the launcher (so notifications reach you), the D-Bus
service (a notification click starts jot), the pixie icon, and the Jot tile.
It asks whether to **start jot at login**, in the background with no window
(`--autostart` / `--no-autostart` answer it up front; `-y` asks nothing). A
running jot is asked to quit first, and a global capture key set in
Preferences is pointed at the installed program. It ends with a list of what
went where. **Run it again to update.**

`uninstall.sh` quits jot, then removes every one of those, the capture key,
and the "jot" calendar in the clock drop-down (a copy jot made). Your
settings in `~/.local/share/jot` -- preferences, Recent jots, captures still
waiting -- are kept unless you say yes (`--purge` without asking).

The older `install-desktop.sh` / `install-extension.sh` still work, for
running straight out of a build tree.

## Backups

On by default, and they need `rsync`. A copy of the open jots folder for each
day, made with `rsync --link-dest`:

```
~/.local/share/jot/backups/
  home.jots/
    2026-10-08/      a whole folder, as it was that day
    2026-10-09/      unchanged files are hard links to the 8th's -- a day
                     costs only what changed
```

Today's is made a few seconds after jot starts (or a folder opens), refreshed
at most hourly while you work, and once more when jot quits or the folder
closes -- each time only if something changed. Kept: the last 7 days, plus
one for each of the 4 weeks before. No day depends on another, so pruning is
deleting folders, and any day opens in Files as it is.

**Preferences › Backups**: on / off, **Where** (Choose… another drive or a
synced folder; Default puts it back), "Last backup: today 09:02 · 9 days
kept", **Back Up Now**, **Restore…**. A chosen place that is not there (a
drive not plugged in) is skipped, said in that line, and tried again later --
jot never makes the folder on the wrong disk.

**Main menu › Restore from Backup…** lists the days; the one you pick comes
back as a new folder beside the open one -- `home (restored 8 Oct 2026).jots`
-- and nothing you have now is touched. Open It, or copy a note back by hand.

## Build

```
./build.sh
./build/jot_selftest     # headless core -- 290 pass / 0 fail
./build/jot
```

jot is one instance and keeps running in the background, so after a rebuild
`./build/jot` reaches the OLD one -- which notices it is older than its
program file, says "an older build was running -- restarting it", and comes
back as the new build. An older jot left open shows a Restart line instead.

Deps (Fedora): `gtkmm4.0-devel spdlog-devel cmake gcc-c++`
Deps (Debian/Ubuntu): `libgtkmm-4.0-dev libspdlog-dev cmake g++`

Optional, for the desktop projection below:

Fedora: `evolution-data-server-devel`
Debian/Ubuntu: `libecal2.0-dev libedataserver1.2-dev`

It is genuinely optional. Without it cmake prints a warning naming the
package, the whole app still builds, and the one feature reports itself
unavailable in the UI rather than failing quietly.

To check the other path deliberately — and this should be done before
any ship, because otherwise only the half this machine selects ever
gets compiled:

```
JOT_BOTH=1 ./build.sh          # builds build/ and build-noecal/
```

`glib-compile-resources` ships with the GLib dev tools the gtkmm dev
package already pulls in.

After touching either the gresource prefix or the app id, verify the
join — they are concatenated silently at runtime and a bad key
resolves to null with no error:

```
gresource list build/jot
```

## Docs

`docs/` in the project root, one level up:

- `ARCHITECTURE.md` — what jot is, and the model the requirements imply
- `ARC.md` — what jot is doing now, open decisions, backlog
- `CANON.md` — what we believe carries forward
- `RULES.md` — standing working rules
- `HANDOFF.md` — session-to-session continuity

## Licence

jot is MIT licensed — see `LICENSE`.

**What jot ships that isn't mine:** one file, `third_party/json.hpp`
(nlohmann/json 3.11.3, MIT), with its licence alongside it in
`third_party/`.

**What jot links:** GTK 4 and gtkmm (LGPL-2.1+), spdlog (MIT), and
optionally libecal / Evolution Data Server (LGPL-2.1+) for the desktop
projection. Dynamically linked, not redistributed here.

**What jot learned from without copying:** the Evolution Data Server
query shape and the `DTSTART` filtering rule come from reading
gnome-shell-calendar-server; what GNOME does to an app with no `quit`
action came from reading `backgroundApps.js`. Both are acknowledged in
the source where they apply.

`THIRD_PARTY.md` has the full account.
