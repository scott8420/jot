# ARCHITECTURE.md — what jot is

## The app in one paragraph

jot is a scribble pad that files itself. You open it and type into a
note; the organising comes after. Notes are markdown, arranged in a
tree where any node can parent any other, with images and media
embedded inline in the markdown body rather than existing as separate
node types. Tasks live inside that markdown as checkbox lines and are
gathered into GTD-shaped views. Nodes link to each other by identity,
so a link survives a rename and a move. GTK4 / gtkmm, C++23, Linux.

**The front door is a blank note, not a tree.** That follows from
"scribble pad": the capture path has to be the shortest path in the
app. Filing is a second act.

**And filing includes choosing where the notes live.** jot with no jots
folder is not an empty app — it is a scratch buffer you can type into
at once, and the question of where it should live is asked when you
leave, not when you arrive. s005 built the other order first and it was
wrong; the rule is strong enough to catch a whole milestone's design,
which is why it is restated here rather than left implied.

---

## Current state (s012)

jot's own surfaces are standing on top of the Cairn spine, reading a
folder of jots through `core::NodeSource`.

| Surface | Files | What it gives us |
|---|---|---|
| Persistence | `core/Project.{hpp,cpp}` | `core::Project`: a `NodeSource` over a jots folder. Atomic writes, deferred bodies, orphan recovery from front-matter ids. |

| Surface | Files | What it gives us |
|---|---|---|
| Node model + the seam | `core/Nodes.{hpp,cpp}` | `Node`, `NodeSource` (reads by id, writes that can refuse, kinded change notification), `MemoryNodes`, the fixtures and the stress generator. GTK-free. |
| The tree | `TreePane.hpp`, `TreePane.cpp`, `TreePane_dnd.cpp` | Pole B flattened: one row per visible node, full timed rebuild, collapse, three-zone drag (above / into / below) with a live-refusing indicator. |
| The note | `EditorPane.{hpp,cpp}` | Markdown body that styles itself as you type, write-through, click-to-tick checkboxes, a protected tell under the body. (The title entry was cut in s006; the name lives in the tree and the drawer.) |
| Capture | `core/Nodes.{hpp,cpp}` (`capture_split`, `capture`), the header line in `Shell_zones/_helpers`, `App::on_command_line` | Text in, an unfiled top-level note out, nothing lost and nothing else moved. One split shared by the header box and `jot --capture`. |
| The pending spool | `core/Pending.{hpp,cpp}`, `App::on_command_line`, `Shell::drain_pending` | Where a capture waits when jot is NOT running: `~/.local/share/jot/pending/`, one front-matter file per thought, written atomically and exiting without a window. Drained oldest-first on the next launch, and ONLY into a jots folder -- the spool file is the only copy until a durable one exists, so flush comes before delete and a crash mid-drain costs a duplicate rather than a note. |
| The task model | `core/Tasks.{hpp,cpp}` | `availability()` derived (parent status, sibling order, done, defer), inheriting due/defer, `next_action()`, date parse/format, `core::TaskIndex` (membership only), grouping by parent. GTK-free. |
| The desktop projection | `core/Projection.{hpp,cpp}`, `Desktop.{hpp,cpp}` | `core::project`: which todos reach the GNOME calendar drop-down and what each says, as a pure function of the model and an instant. `Desktop`: the only file that includes libecal — creates a local EDS task list, wipes rows with the `jot-` uid prefix, writes the set. One way, always. Optional at build time. |
| Due notifications | `core/Notify.{hpp,cpp}`, `Shell_helpers.cpp`, `install-desktop.sh` | `core::due_announcements`: which todos deserve an INTERRUPTION, as a pure function of the model and an instant -- and it filters by availability where `core::project` deliberately does not. A 60s tick; `Prefs::announced` remembers what has been said and survives a restart. Click lands on `app.goto-node`. Needs the installed `.desktop` entry, icon and D-Bus service, or GNOME drops it silently. |
| Residency | `core/Lifecycle.{hpp,cpp}`, `Shell.cpp` (the close handler), `Shell_helpers.cpp` (`request_quit`), `App.cpp` (`app.quit`) | `core::on_close` / `core::on_quit`: what the X means and what Quit means, as a pure truth table -- three answers where there used to be one, and two of them differ only in whether unsaved notes are about to be lost. Off by default. A vetoed close plus `g_application_hold()`; the Shell, the store and the 60s timer all survive the window. `app.quit` is MANDATORY, not decoration: GNOME's Background Apps Quit activates it over D-Bus and SIGKILLs an app that has not got one. |
| Today | `TodayPane.{hpp,cpp}` | The report: Overdue pinned, then Today / Available / Flagged grouped by parent with the parent note's first line as the why. |
| The markdown scan | `core/Markdown.{hpp,cpp}` | `core::scan`: blocks, inline runs, task checkbox ranges, byte↔codepoint offsets. GTK-free, 39 selftest checks. |
| The window | `Shell*` | `Paned` tree\|note, note actions greyed by selection, the model→surface callback. |
| Where the jots live | `Shell_{zones,helpers,handlers}.cpp`, `JotsFolderDialog.*`, `core/Project.*` | No default folder: a scratch buffer until named. `<name>.jots` folders, the header title menu button carrying name + path, adopt (scratch → folder), rename and relocate — both a rename of one directory, because nothing inside holds a path. |

`Shell::m_store` is typed as the INTERFACE; `Shell::m_project` is the
concrete handle, and exists only for the two things a jots folder can
do that a `NodeSource` cannot: flush, and name its own directory.
Constructing it is the swap point, and it was the whole of what s003
had to touch.

What the spine underneath carries, unchanged from s001:

| Surface | Files | What it gives us |
|---|---|---|
| `Named<W>` + live registry | `include/widgets/`, `Registry.*` | Mandatory widget names, dotted (`shell.sidebar`), `registry::dump()` for the live tree. `unregistered_t` ctor for per-row content. |
| Split class by category | `Shell.hpp` + `Shell_{zones,bindings,handlers,helpers}.cpp` | The header-as-index discipline, installed on day one rather than retrofitted. |
| Per-area logging | `Log.*` | spdlog named loggers, runtime-toggleable. Areas: App, Shell, Registry, Recents, Io. |
| Actions + parameterised actions | `Shell_bindings.cpp` | Gio action wiring, including the per-item recents action group. |
| JSON persistence pump | `core/Recents.*` | Encode/decode adjacent in one file. The pump discipline, with a working instance. |
| Forward/inverse text mapping | `core/TextMap.*` | Flattened-line ↔ codepoint-range mapping with a round-trip test. Directly relevant to the markdown editor. |
| Resource pipeline | `resources/`, `App.cpp` | Symbolic icon compiled into the binary, recoloured by theme. |
| Dialog lifetime | `AboutWindow.*` | Hide-on-close singleton, built lazily, owned by Shell. |
| Desktop appearance | `Appearance.*` | Follows the desktop light/dark preference via the XDG portal, with the GSettings fallback guarded. |
| Shortcut registry | `core/Shortcuts.*`, `ShortcutsDialog.*` | One list, two consumers: App wires accels from it, the dialog renders from it. Can't drift. |

**Cairn's demo pages are gone**, replaced by the real UI in s002. The
recents menu and its JSON pump survive without a job: they become
"recent vaults" the day D1 is answered, and a live exemplar is worth
more than a deleted one.

**Not present, and needed:** the metadata drawer and its pane toggles;
image and media drop; sibling reorder (drop-between);
backlinks; the GTD query views. GTK-side, still untouched:
`ListView`/`ColumnView` and the factory stack (Pole A — see D6),
`DrawingArea`, `Popover`.

### Build

```
./build.sh
./build/jot_selftest     # headless core, currently 305 pass / 0 fail
./build/jot
JOT_STRESS=5000 ./build/jot   # the D6 instrument: a synthetic vault
```

Deps: `libgtkmm-4.0-dev libspdlog-dev cmake g++`. Optional, for the
desktop projection: `libecal2.0-dev libedataserver1.2-dev` (Fedora:
`evolution-data-server-devel`). The PROBE additionally needs the running
daemon (`evolution-data-server`), not just the headers. `pkg_check_modules` is NOT REQUIRED for
it — without it the app builds whole and the feature reports itself
unavailable, which is what keeps an optional dependency optional. The
first dependency outside gtkmm/spdlog, and the reason there is a
`jot_desktop_probe` target: the writing half cannot be observed without
a session bus, so it has its own instrument rather than being taken on
trust. BOTH halves of the `#ifdef` are built before any ship
(`cmake -B build-noecal -DJOT_DESKTOP=OFF`); s009 shipped a fallback
branch nothing had ever compiled. Verified against
gtkmm 4.10.0 / g++ 13.3 / cmake 3.28 in the sandbox (Ubuntu); Scott
builds on Fedora, so sandbox-green is evidence, not proof.

For notifications: `./build.sh --install-desktop`, once. App id casing
is now LOAD-BEARING -- the `.desktop` basename must match it exactly or
the notification daemon drops everything jot sends.

App id: `io.github.scott8420.Jot`. GResource prefix mirrors it:
`/io/github/scott8420/Jot/icons/scalable/apps`. Both move together in
a one-pass rename. Verify the join with
`gresource list build/jot` after touching either.

---

## The model, as the requirements imply it

Recorded so the shape is visible while the forks below are still open.
**The node and the move are shipped (s002); links, tasks and the GTD
views below are still a sketch.**

### Nodes

**Shipped in s002 — this is code now, not a sketch.** The sketch below
is what got built, with `body` as a plain string and the flags reduced
to one (`protect`).

A node is the only entity. Every node can parent any number of
children; there is no folder/document distinction. (Notr had one, and
its whole drag-and-drop limitation traced back to it.)

```
Node {
  id          stable identity, generated once, never reassigned
  parent_id   empty for roots
  title
  body        markdown; images and media referenced inline
  created / modified
  ...per-node flags (a protect/lock flag is worth carrying from Notr)
}
```

**The tree is a projection of a flat node set**, reconstructed by
parent lookup. Notr had this right and then didn't use it: its drop
handler copied a node, appended the copy, and deleted the original,
which is why it could not move a subtree and would have broken every
inbound link the moment links existed.

**A move is one field write: `parent_id`** (plus a position among the
new siblings, since s002's drop-above/drop-below gesture has to mean
something the store can write down). Children come along because they
never referenced anything but their parent. This is
CANON's "tracking is an illusion; the model is the truth," and with
links in the picture it stops being a matter of taste — identity has
to survive a drag.

### Links

Links target **id, never title or path.** Titles get renamed and tree
position changes on every drag; a title-keyed link rots silently.

Candidate syntax `[label](jot:<id>)` — valid markdown for any outside
tool, human-readable label, stable target. If notes become files on
disk, the id must also live in front-matter so the file carries its
own identity.

**Backlinks are the feature.** A forward link is a convenience; the
reverse index ("what points at this note") is what makes a linked
structure worth having. One pass at load, maintained on save.

### Tasks

**Shipped in s007. The sketch this section used to carry — "checkbox
lines anywhere in any body are tasks, and the GTD views are queries over
every checkbox line" — was WRONG and is recorded as overturned rather
than quietly replaced.**

What it got right: the views are queries, not a second store. What it
got wrong: a checkbox line has no identity, and **you cannot hang a
field off something with no identity.** A due date, a defer date and a
flag need something to belong to.

So a **todo is a NODE** (D2), with optional task fields:

```
Task {
  is_task   the presence bit -- this node is a todo
  done
  due       epoch seconds, 0 == none
  defer     do not surface it before this
  flagged   "this one, today"
  status    None | Sequential | Parallel -- how this node's CHILDREN run
}
```

A node is a todo when `is_task` is set and a note when it is not — the
same no-split principle as requirement 7, applied again. Everything
built since s002 comes free: ids, links, backlinks, children, a markdown
body, the drawer, tags, drag-reparent, protection.

**Task state lives in `jot.json`** (D4), alongside the title and the
parent. The note files on disk carry none of it.

**No priority field.** Numeric priorities rot — within a month
everything is a 1, which is why OmniFocus dropped them. jot uses the
three things that do not: sibling order (already model state since
s002), `flagged`, and `defer`.

**Availability is DERIVED and never stored** — a pure function of
(parent status, sibling order, done flags, defer dates), living in
`core::Tasks` under the selftest. A stored availability flag is a cache,
and a stale one hides a task, and **a task wrongly hidden is a task you
do not do** with nothing on screen to say so.

`core::TaskIndex` holds membership in document order and no verdicts.
Today, Available, Flagged, Overdue and Scheduled are filters over it.

A checkbox line in a body (`- [ ] milk`) is still a real thing and still
ticks — it is an INFORMAL note to yourself that the report ignores. Two
todo systems that both look real, where only one reaches the report, is
how you stop trusting the report.

### Widget naming vs node identity — two vocabularies, kept apart

Widgets get dotted names (`shell.sidebar`, `editor.body`) in the live
registry. Nodes get ids. **These are different systems and should stay
visibly separate** — a future session must not try to address a note
through the widget registry. Notr blurred this line and it's part of
why its drag handler reached for tree-control APIs instead of the
model.

Per-row widgets in the tree view take `unregistered_t`: named for the
Inspector, not entered in the registry, because a `ListView` rebuilds
row content on every scroll and reorder.

Worth building early, alongside `registry::dump()`: a **model-side
dump** that prints the node tree with ids and link targets. Same
debugging payoff, other layer, and it's what you want the first time
a link resolves to nothing.

---

## Open architectural decisions

These are unresolved and everything else waits on the first one. See
ARC.md for status.

0. **(answered by looking, not yet decided)** D1 below is now the
   gating question for s003 and is answerable from the running app.

1. **ANSWERED (s003). Where the markdown lives** — one document (Notr's shape,
   modernised: one file, tree inside it, markdown as node bodies) vs
   a folder of markdown files (one `.md` per note, tree membership in
   front-matter or directory structure). Import/export and "images
   dropped into markdown" both push toward the vault; atomic save and
   simplicity push toward the single document.
2. **ANSWERED (s007). Task representation** — checkbox lines harvested by query
   (above) vs tasks as nodes with subtasks. Both are defensible;
   having both without deciding which is canonical produces two
   half-task-systems.
3. **ANSWERED (s004). libadwaita or hand-rolled GTK4** — no
   libadwaita, and the reason is the binding rather than the look:
   there is no official C++ binding and none planned, so adopting it
   from C++ means calling the C API by hand or vendoring an unstable
   third-party one. Folio has never used it either — its `adw_*`
   strings are variable names over ~1,700 lines of hand-written
   stylesheet. That leaves adw offering only its stylesheet, which is
   the one thing Scott has declined for a decade of sessions. Full
   reasoning in the s004 handoff. Original text below.

   **(original)** libadwaita or hand-rolled GTK4 — "modern GNOME look" visually
   *is* libadwaita (`AdwNavigationSplitView`, `AdwToastOverlay`,
   `AdwPreferencesDialog`, boxed lists). No prior project links it;
   `Appearance.cpp` exists precisely because they don't. A
   sidebar-plus-content notes app is the strongest case yet for
   adopting it.
4. **GTD state location** — inline in the markdown
   (`- [ ] call Bob @waiting due:2026-10-14`) keeps the file portable
   and is where Obsidian/Logseq converged; a sidecar index queries
   faster and keeps prose clean. Inline plus a rebuilt-on-load cache
   is the likely answer but it should be decided, not discovered.
5. **ANSWERED (s004). Does Folio's markdown editor lift?** No — Folio
   has no markdown editor. Its `Editor` is WYSIWYG over a
   `GtkTextBuffer` serialized to HTML, and markdown lives only at its
   import/export boundary with no inline parsing in either direction.
   jot's storage format IS markdown, so the HTML round-trip is exactly
   the layer we don't want. jot's scanner is its own
   (`core/Markdown.*`); what lifted from Folio was the idle-restyle /
   double-guard / never-fight-the-user discipline of its screenplay
   classifier. Full reasoning in ARC.md.
