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
```

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

**There is no daemon** — no autostart, nothing started on your behalf.

With text, the window is deliberately *not* raised: filing a thought
from a script or a keybinding should not throw a window in front of
whatever you are doing. Without text there is nothing to file, so the
only sensible reading is "let me type", and that does raise it.

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

`JOT_STRESS=5000 ./build/jot` loads a synthetic set instead of the
fixtures, and every tree rebuild logs its row count and milliseconds to
the `tree` log area. That is the D6 instrument.

`JOT_DEBUG=drawer,tree ./build/jot` raises those log areas to DEBUG
(`all` for every area, `trace:<area>` for one louder). The
instrumentation is always in the binary; this is the switch.

What the Cairn spine underneath carries: mandatory widget naming with a live registry,
a split class with the header as its index, per-area runtime-toggleable
logging, Gio actions including parameterised ones, a JSON persistence
pump with its encode/decode adjacent, forward/inverse text mapping with
a round-trip test, the compiled-in resource pipeline, hide-on-close
dialog lifetime, desktop light/dark following, and a shortcut registry
with one source and two consumers.

## Build

```
./build.sh
./build/jot_selftest     # headless core -- 290 pass / 0 fail
./build/jot
```

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
