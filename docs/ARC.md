# ARC.md — what jot is doing now

## Phase

**s013 finished capture (done).** `jot --capture "milk"` with jot NOT
running writes the thought into `~/.local/share/jot/pending/` and exits
WITHOUT EVER MAKING A WINDOW; the next launch drains it, oldest first.
The other half of s008, four milestones late, and s012 shrank it on the
way: with residency on, a capture usually finds a live jot over the bus
and this is the fallback.

It also fixed the wrinkle s012 created: `App::on_activate()` presented
unconditionally, so a scripted capture arriving at a resident jot with
no window YANKED THE WHOLE APP ONTO THE SCREEN. Activation and
presenting are two things now, not one statement.

**s012 made jot resident (done, after being broken and fixed in the same
session).** With "keep jot running when the window is closed" ticked, the
X hides the window and the process -- and the minute clock -- keep going.
`g_application_hold()`, no daemon.

**It shipped broken.** A hidden window returns NULL from
`get_application()`, so every notification sent while resident was
silently skipped by an `if (app)` guard, and the re-presented window came
back with dead widgets because it could not resolve `app.*`. One cause,
two symptoms, four hours. Fixed: send through
`Gio::Application::get_default()`, re-attach with `add_window()` on
present. **Seen working in both channels.**

The milestone's designed work was not the hold: it was that the
unsaved-notes prompt had to MOVE OFF THE CLOSE AND ONTO THE QUIT, and
the quit path has no window to put a dialog on. It presents one.

**s011 built due notifications (done).** An available todo whose deadline
arrives gets a desktop notification, and clicking it opens jot on that
task. `core/Notify` decides and words; `Shell` sends. On by default.

The milestone exists because of Scott's question at the end of s010: can
you click a row in the GNOME calendar drop-down and land on the task?
**No** -- `EventsSection` in `dateMenu.js` is ONE `St.Button` whose
`vfunc_clicked()` launches the `text/calendar` handler with no arguments.
Click-through is a property of notifications, not of the calendar card,
which sharpens the s010 decision to keep both: they are not redundant
coverage, they have different POWERS. The calendar is read-only
visibility while jot is CLOSED; the notification is the one you can act
on.

**Before it, s010 rewrote the projection to write VEVENTs into an EDS
CALENDAR (done), and it was finally SEEN working** -- jot's dated todos
on their days in the calendar drop-down, the first convergent evidence
this feature has had in two attempts. The first connect is async; the
thirty-second freeze is gone.

Behind those: **s009 built the projection's decision half, which
survived the rewrite unchanged.** **s008 built capture.** **s007 built
the task model** -- todos are NODES (D2), task state lives in `jot.json`
(D4), availability is DERIVED under the selftest. **s006 added the third
pane**, the F9/F10 toggles, the backlink index and Save / Save as...
**s005 removed the default jots folder.**

D1 through D5 are answered. **D6 (Pole A vs Pole B) is the last one
open**, and it stays open until a real corpus says whether a full
rebuild is cheap enough -- which is one more reason the migration
question below is load-bearing.

---

## s013 — pending capture (done)

**The thought you have while jot is closed.** `jot --capture "buy milk"`
with no running instance used to LAUNCH THE APP -- a window, a tree, a
jots folder opened, all so one line could be written down. Now it is
spooled and the process exits; the next launch files it.

`cmd->is_remote()` is the whole test. False means this process is the
primary instance, which means there was nothing running to forward to.
No daemon, no autostart, no second lifecycle -- the fallback is a folder
of files and a drain, and that is all it is.

### The spool is the only copy, and every rule here comes from that

`~/.local/share/jot/pending/`, one front-matter file per thought, beside
`recent.json` and `prefs.json` because it belongs to the APPLICATION
rather than to any one set of notes. **Not inside the jots folder:**
`jot.json` with two writers is corruption, and a capture can happen
before any jots folder has been chosen at all.

Three consequences, and they are the design:

- **The write is atomic** -- temp file plus rename -- so a drain running
  at that instant reads a whole thought or no file, never half of one.
  `.part` files are skipped by the reader.
- **The read never deletes.** `read_pending` and `remove_pending` are
  separate calls, and the delete happens only after the note exists
  somewhere that outlives the process. A crash mid-drain costs a
  DUPLICATE, never a note, and that is the correct way round.
- **The drain only runs into a jots folder.** Draining into the scratch
  buffer would turn a capture that was safely on disk into one that
  Discard-on-the-way-out takes silently. With no folder open the spool
  simply waits; the three callers -- first launch, Open, and the Save
  that gives the scratch buffer a home -- are exactly the three moments
  a folder appears.

And one layer down, the same rule: `m_project->flush()` runs BEFORE any
spool file is removed.

### Tolerant on the way in

A file with no front matter is read as a bare thought rather than
skipped. Somebody echoing a line into that folder from a shell script
has said something, and a capture spool that ignores what it does not
recognise is a capture spool that loses notes. An empty file is not a
capture; an unterminated header is not a header; a capture containing
`---` is not cut at it (it is markdown -- a horizontal rule in a
captured note is not exotic).

### `ensure_shell(present)` -- the s012 wrinkle, fixed

`on_activate()` is a one-liner onto it now. What must always be true
before a capture can land (a Shell exists, it is ATTACHED to this
application) happens either way; `present()` happens only when something
asked to be looked at. The re-attach of a detached Shell is on the
silent path too -- a capture still has to land somewhere whose `app.*`
actions resolve.

The log says `silent arrival -- the window was left exactly as it was`,
which is the line to grep for, because the other channel for that claim
is permanently Scott's eyes.

### Seen working, and further than usual

The sandbox has no display, so this milestone would normally ship on a
green build alone. Xvfb plus `dbus-run-session` reached further:

- two cold captures spooled, `exit=0`, no window, and the terminal said
  so. Same second, two pids, no clobber.
- next launch: `drained 2 pending capture(s)`, the spool empty, both
  notes in `jot.json` with their titles **in the order they were thought
  of**.
- and in one session: cold capture -> spool -> drain on launch, then a
  warm `jot --capture` into the running instance logging `silent
  arrival` and landing the note.

**That is the trace channel, further along than usual, and still not the
visual one.** Nothing here proves a hidden jot stays hidden -- an Xvfb
window nobody looks at cannot report being yanked forward.

`jot_selftest` **367 pass / 0 fail**, up from 350: eighteen on the
spool, because the parse of a file that holds the only copy of what
someone typed is the one place a silent `return {}` would be a lost
note. Both halves of the desktop `#ifdef` built.

### Also in this session: jot went into git

**MIT for jot itself.** Dynamic linking to LGPL gtkmm constrains
nothing, so every option was open; MIT is the fewest conditions on
someone reading the code, which is what a small personal app wants. One
file to change if that judgement ever looks wrong.

**One obligation, and it was real.** `third_party/json.hpp` carries an
SPDX identifier but not MIT's permission text, which MIT requires to
travel with copies. `third_party/LICENSE-nlohmann-json.txt` fixes it.

**Everything else is courtesy, and courtesy was paid.** GTK/gtkmm,
spdlog and libecal are linked, not distributed, so a source repo owes
them nothing today -- but a binary or flatpak does, and `THIRD_PARTY.md`
keeps the three categories (shipped / linked / read) apart so the day
that changes is a packaging question rather than an archaeology one.
gnome-shell-calendar-server got a "read, not copied" entry: the EDS
query shape and the `DTSTART` filter were learned from its source and
kept deliberately identical, which is worth saying out loud even though
no code moved.

**One repo, docs under `docs/`.** A behaviour change and the handoff
explaining it belong in one commit.

### Known and NOT fixed

**A drained note's `created` is the drain time, not the capture time.**
The true time is in the spool file's front matter and nowhere else --
writing it onto the node needs a `NodeSource` seam change (there is no
`set_created`), which is not this milestone's business. A thought
captured Tuesday and drained Wednesday says Wednesday.

---

## s012 — the background notifier (done)

**A notification is only as useful as the app's uptime.** Scott's call,
made while watching s011 work: nothing fires while jot is closed, and
the first notification he ever saw was a 16:01 deadline announced at
16:05 -- when jot STARTED, not when the deadline passed.

`g_application_hold()` and a vetoed close. No autostart, no second
lifecycle, no supervision, nothing installed beyond what s011 already
installed.

### The claim that was false, and was written as fact

**GNOME does NOT list jot under Background Apps.** It was asserted in
the s011->s012 handoff, believed through the build, and disproved by
looking: Surfshark -- a flatpak -- appears the moment its window closes;
jot never does, with the same `.desktop` entry its notifications route
through successfully. The list comes from xdg-desktop-portal's
background monitor, which appears to enumerate Flatpak instances.

The hold works, the notifications work, Quit works. What is gone is the
HONESTY argument -- "the user can see jot running and stop it where they
see everything else." That is an argument for the preference staying OFF
by default, not against the feature.
`org.freedesktop.portal.Background.SetStatus` is unexplored.

### The bug, which is worth more than the feature

```cpp
if (auto app = get_application()) app->send_notification(a.id, n);
if (auto lg = ...) lg->info("notified: ...");   // OUTSIDE the guard
```

A hidden window has no application. The send was skipped and the log
said it happened -- and `g_application_send_notification()` returns
void, so there was no return value to check even if it had been called.
Every measurement downstream of that line pointed the wrong way for four
hours, at the desktop rather than at jot.

Scott named the cause two hours before the instrumented build confirmed
it: *"I think it's the background part of jot that's mucking it up."*

### The prompt moved, and that was the milestone

`guard_scratch()` hung off the window close because close meant exit.
Once close means "keep running" that prompt is A LIE -- nothing is being
lost, the notes are still in memory with the process holding them -- and
a dialog that cries wolf is one the user learns to dismiss before the
day it matters.

So the ask moves to the QUIT, which is the path that has no window to
put a dialog on. **It presents one.** A quit that stops to ask is
startling exactly once; discarding an afternoon's notes because the
process happened to have no surface at the time is worse than startling.

### `app.quit` is not decoration, and this is the finding

**GNOME's Background Apps Quit does not close a window. It activates the
application's `quit` action over D-Bus -- and an app that does not have
one gets SIGKILLed instead.** (KDE hit the same thing on the code they
copied from gnome-shell's `backgroundApps.js`.)

SIGKILL cannot be caught. For a resident jot that means an unsaved
scratch buffer dying with no prompt and no flush, in precisely the
situation this milestone exists to handle. **Residency and that action
have to ship together, or residency is a data-loss feature.**

Same reasoning as `goto-node` for living on the APPLICATION: the request
arrives at a process whose window may have been closed an hour ago. It
does NOT `activate()` first, though -- a quit is not a reason to build a
window.

### `core/Lifecycle`, and why a truth table

Closing used to mean one thing, so the branch could sit inline in the
close handler and be read at a glance. It means three now, **the
difference between two of them is whether unsaved notes are about to be
lost, and the wrong answer is SILENT.**

So the decision is pure and the doing is GTK -- the same seam as
`core::Projection`/`Desktop` and `core::Notify`/`Shell`, one surface
further on. Every row is asserted by IDENTITY, never by counting, which
is s011's banked lesson applied on the first try instead of the second.

Two rows are the design rather than bookkeeping:

- **Residency outranks the scratch prompt.** Close with unsaved notes
  and residency on does NOT ask.
- **A quit in progress outranks residency.** Without that row the Quit
  item would HIDE the window instead of destroying it, and jot would
  refuse to stop from the one menu GNOME offers for stopping it.

And one non-row: `on_quit` never consults `background`. The preference
decides what the X does, never what Quit does. An off switch a
preference can disarm is not an off switch.

### Hiding, not destroying

The close is VETOED and the window made invisible. The Shell, the store,
the scratch buffer and the 60-second timer all stay exactly as they
were -- **a Glib timeout does not care whether a window is on screen**,
which is the whole trick, and `present()` brings the same window back
with the tree still expanded the way it was left.

The hold has ONE writer and is idempotent, mirrored by `m_holding`:
GApplication COUNTS holds, so two holds and one release is a process
that never exits, and one release too many takes the count negative
under a window somebody is using.

### The same lookup, answering a second question

`m_notify_ok` -- can GNOME find jot's `.desktop` entry -- was asked for
notifications. It turns out to be the same prerequisite for being LISTED
under Background Apps, because both go through the application id. One
lookup, two features, one silent failure mode, and the new status line
says so.

### The footer has three boxes now

Residency goes UNDER the other two, in the same footer, because it is
the PRECONDITION for both: a calendar row is written while jot runs and
a notification is sent while jot runs. It is the switch you reach for
once the other two have disappointed you. A preferences window would
have been the alternative and jot does not have one yet.

The status line says the CONSEQUENCE, not the setting -- with residency
off, a closed jot announces nothing, and the line above it cannot say
that. With residency on and no jots folder it says the notes are held in
memory until you quit, which is the "resident with no jots folder" worry
said out loud before it can surprise anybody.

### And the ten-minute fix, first

The notify status line counted KEYS. Now it reports SENDS, with a clock
on them: `On. Sent "Ring the vet" at 16:12.  2 open deadlines.` The
desktop line directly above has carried a clock time since s009; the
answer to "why did that arrive when it did" is the timestamp and nothing
else.

`jot_selftest` is **350 pass / 0 fail**, up from 305.
`jot_desktop_probe` still **14 pass / 0 fail**. Both halves of the
desktop `#ifdef` built before shipping. **Green on the first build.**

---

## s011 — due notifications (done)

- **`core/Notify.{hpp,cpp}`** -- the decision half, pure and GTK-free, on
  `core::Projection`'s seam and for the reason s009/s010 proved: a
  delivery mechanism was rewritten wholesale and the decision half did
  not change a line.
- **A projection is state; a notification is an EVENT**, and that drove
  everything. It needs a CLOCK (nothing in the model changes when a
  deadline merely arrives -- 60s tick, as fine as a jot deadline can be
  expressed). It needs a SECOND fingerprint, and a LIST rather than a
  string, because "what has already been said" is per-task and must
  survive a restart. `Prefs::announced`, pruned every check to exactly
  the set currently due.
- **The key is `<id>@<due>`.** Rescheduling is the most common edit a
  todo gets and keying by id alone would have made it silent.
- **It filters by AVAILABILITY where the projection deliberately does
  not.** A calendar card is a day grid and dropping a row would make it
  lie about the day; an interruption about something you cannot act on
  is what teaches a person to turn notifications off. **A grid may not
  lie by omission; a nudge may be selective.** Both directions are under
  the selftest.
- Undesigned and welcome: finishing a step in a Sequential project makes
  the next one available, and if it was already due it announces itself
  on the following tick. Availability is derived, so the clock is enough.
- **Overdue means a DAY has passed, not a second**, or every notification
  jot ever sent would be an overdue one. Overdue goes out URGENT and
  survives Do Not Disturb.
- **GNOME routes notifications by APPLICATION ID and drops silently what
  it cannot look up.** jot runs from a build tree, so the default failure
  mode would have been "works perfectly, shows nothing". jot performs the
  daemon's own lookup (`Gio::DesktopAppInfo::create`) at startup and the
  Today footer says so. `./build.sh --install-desktop` installs three
  files under `~/.local/share`: the desktop entry (the lookup, and what
  puts jot in Settings > Notifications), the ICON (a GResource is
  invisible to the Shell, which is the process that DRAWS the
  notification), and the D-BUS SERVICE file (so a click after jot is
  closed starts it -- `DBusActivatable=true` without one is worse than
  neither, because GNOME stops falling back to `Exec`).
- **`app.goto-node` is an APPLICATION action**, because a notification
  outlives the window that sent it. `Shell::goto_note()` is a thin public
  door onto the same reveal-then-select drawer links use.
- Notifications are keyed by node id, so a second one about the same task
  replaces rather than stacks.
- **Two boxes in the Today footer now**, and the notification one is ON
  by default where the projection's is OFF: the projection writes into
  another application's database, a notification writes nothing.

`jot_selftest` **305 pass / 0 fail**, up from 290. Probe unchanged at 14.

**SEEN WORKING the same day.** A todo due 16:12 produced a jot
notification stamped "Just now" at 16:12, with jot's own icon, in a live
GNOME session. With s010's calendar row -- also seen -- that is two
milestones running that reached BOTH evidence channels, after three that
never reached the visual one.

**Nothing fires while jot is closed**, and finding that out is what
prompted s012: the first notification seen was a 16:01 deadline
announced at 16:05, when jot STARTED. Correct behaviour, and the whole
argument for a resident app.

**Three selftest checks failed on the first run and all three were the
TEST's fault** -- count assertions over a fixture whose arithmetic had
been done in my head. Rewritten to name nodes rather than count them.

**Deliberately not in s011:** "Mark done" as a notification action. It is
the strongest argument notifications have over a calendar row and it is
cheap, but it is a write path from the tray into a model that may have
moved, and a milestone ends at a surface.

---

## s005 — where the jots live

Three faults, all of them about the app's front door, and the third
was my call in s003.

- **`core/Project.{hpp,cpp}` grew the folder operations**, GTK-free
  and tested before a widget existed: `relocate_jots()`,
  `is_jots_dir()`, `jots_note_count()`. 15 selftest checks on real
  temp directories (131 pass / 0 fail, up from 116).
- **There is no default jots folder.** Not a better path — none.
  `open_store()` touches no disk when there are no recents; the store
  is an in-memory SCRATCH BUFFER and the app is a pad you can type
  into immediately. `default_jots_location()` (~/Documents via the XDG
  special dir) is only the folder the naming dialog opens in.
- **Folders are named `<name>.jots`.** `core::jots_folder_name()`
  appends the suffix and refuses a name that can't be one (blank, a
  path separator, `.`/`..`); `core::jots_display_name()` strips it for
  the header. The suffix is never typed and never silently rewritten.
  It is a CONVENTION, not a requirement — `is_jots_dir()` still
  recognises a folder by its contents, so hand-made folders and
  pre-suffix ones like `vault` open normally.
- **`Project::adopt()`** — the scratch buffer's Save. Re-mints every id
  as a uuid (a Project's ids become filenames; `n0001` must never
  reach one), remaps every parent through the same map, preserves
  sibling order, bodies, flags and timestamps. Refuses a folder that
  already has notes. The test that matters is the reopen at depth two:
  a bad remap is a silently-flattened tree.
- **`guard_scratch()`** — one prompt on every exit from an unsaved
  scratch: window close, Open jots…, and a recents pick. Save /
  Discard / Cancel, Save as the default because Discard here is
  unrecoverable. The Save arm only continues if the save took.
- **`JotsFolderDialog`** — one dialog, three occasions (New, Save,
  Rename), with a live preview of the path that will be created.
- **Rename jots…** — a move that stays put, same `relocate_jots()`.
- **The header title is a menu button.** The folder name over the
  folder it is in; the absolute path is the menu's section label, with
  Open in Files, Copy path and Relocate jots… under it.
  `update_jots_title()` is the single writer of all four places the
  location appears (two labels, the section label, the tooltip) plus
  the window title, so they cannot disagree. With no folder it reads
  "Unsaved jots / not saved to disk yet"; under JOT_STRESS, "Stress
  test / synthetic — nothing is saved".
- **Relocate is a rename of one directory.** Nothing inside a jots
  folder holds an absolute path, `jot.json` addresses by id, filenames
  are uuids — so the move reads and rewrites nothing. `relocate: the
  tree survived, addressed by id` is the check that proves it. Notr
  could not have survived this operation.

**Every refusal is tested to leave the source untouched** — missing
source, occupied destination, moving a folder into its own subtree.
The cross-filesystem branch (EXDEV) is copy-VERIFY-remove: if the copy
comes up short, the original stays. A half-moved jots folder is the one
outcome that function must never produce, and it is the branch the
selftest cannot fully reach, since `temp_directory_path()` offers only
one filesystem.

**Dead code removed:** `src/core/Vault.cpp` and
`include/core/Vault.hpp`, 441 lines of the pre-rename copy of
`Project`, unreferenced by CMakeLists since s003 and therefore never
compiled and never tested.

**The design changed mid-session, after Scott ran the first half.** It
had shipped with `~/Documents/jots` as a suggested default and a
first-run dialog asking where notes should live. That dialog put a
modal in front of the blank note — exactly the inversion
ARCHITECTURE's second paragraph warns about (*capture first, file
second*), written in the same session that quoted it. The scratch
buffer is the corrected shape: the filing question arrives at close,
when it is a real question. `FirstRunDialog` is deleted.

**Not shipped, deliberately:** the metadata drawer, the pane toggles,
backlinks, and retiring the status line. The status line still carries
the parent and the protected state and nothing else does yet; it is
retired in s006 when the drawer takes those over.

---

## s003 — persistence, and the swap

- `core/Project.{hpp,cpp}` — `core::Project`, a `NodeSource` over a
  folder. Encode and decode adjacent in one file (CANON: pumps at
  conceptual seams), with the round trip tested against a real temp
  directory including **sibling order**, which is the half a naive
  format loses.
- **D1, as chosen:** a folder of markdown files with a project file
  inside it building the tree. Flat `notes/`, `<uuid>.md` filenames,
  `attachments/` for media. `jot.json` owns parent, order, title and
  flags. Each note carries its own `id` in front-matter — for
  recovery, not interchange; export is what makes human-readable
  files.
- **A move writes `jot.json` and nothing else.** No rename, no `mv`,
  no stale path. The Notr failure mode has no filesystem operation
  left to get wrong.
- **Recovery is tested, not asserted:** delete `jot.json`, reopen, and
  every note returns as a top-level orphan under the id its file
  carries, with the project file rewritten on the way out.
- Structure writes immediately; body edits are deferred to a 2-second
  timer, a selection change, and window close. Ctrl+S forces it.
- New jots… / Open jots… / Recent jots / Save all. "New" and "Open" do
  the same thing to a folder but are different intentions, and
  collapsing them (my call in the first draft) made starting a new set
  the longer path.
- **The vocabulary is "jots"**, not "vault" — Obsidian owns that word.
  `core::Project` in code, so nothing has to write "a jots".
- 20 new selftest checks (77 pass / 0 fail).

**The swap test passed.** `TreePane` and `EditorPane` changed by zero
lines. The only edit below the seam was a protected virtual
(`mint_id`) so ids could become uuids; the only edit above it was in
`Shell`, which IS the swap point.

---

## s002 — the fixture surface (superseded by the above, kept for the record)

**Fixture surface.** jot's own surfaces exist and are lookable: a tree,
a note pane, and drag-and-drop reparenting, all backed by hardcoded
fixtures through `core::NodeSource` and **no persistence whatsoever**.
Quitting throws the session away, by design. D1 is now the thing to
decide, and it is decidable by using what's on screen rather than by
argument. See *Hot for next session*.

---

## s001 — project founding

Shipped:

- `jot-gtk-project/{backups,docs,fixes,scripts,jot}` laid out to the
  standard project shape.
- `scripts/jot_backup.sh` — allowlist-based, paths derived from the
  script's own location, previous zips retired to `backups/Old/`.
- The Cairn spine seeded into `jot/` and renamed end to end
  (namespace, targets, app id, gresource prefix, icon, display
  strings). Builds clean; `jot_selftest` 30 pass / 0 fail; gresource
  join verified with `gresource list`.
- Four Cairn defects fixed in the transplant: C++17 → C++23; a
  `build/` directory that was riding in the backup zip; `.gitignore`
  widened; the docs replaced rather than carried (Cairn shipped
  Curvz's `RULES.md` and `CANON.md` verbatim, titled "Curvz working
  rules" — about 85% of that CANON was Curvz-specific).
- `docs/` — RULES, CANON (distilled to the portable subset),
  ARCHITECTURE, ARC, HANDOFF.
- New symbolic logo (page with a folded corner and two jotted lines).

Not shipped, deliberately: any jot domain code. See open decisions.

---

## s002 — the fixture surface

Shipped:

- `core/Nodes.{hpp,cpp}` — `Node` (id, parent_id, title, body,
  created/modified, protect) and **`NodeSource`, the seam**. Reads are
  by id; writes return false when the model refuses (cycle, protected
  node, unknown id), because a drag onto an illegal target is a thing
  users do constantly.
- `MemoryNodes`, the only implementation: a flat vector plus a
  parent→children index. `Shell::m_store` is the single place in the
  codebase that names a concrete implementation. Everything else holds
  a `core::NodeSource*`.
- Change notification carries a KIND
  (`Reload/Created/Removed/Moved/Title/Body/Flags`), not just an id.
  Not decoration: a tree redraw is a full rebuild, so a `Body` change
  must not trigger one or every keystroke in the editor repaints the
  whole tree.
- `TreePane` + `TreePane_dnd.cpp` — Pole B, flattened (one row per
  visible node, indented, full rebuild on every structural change,
  `Gtk::ListBox` doing selection and keyboard nav). Collapse is "don't
  emit the descendants".
- **Three-zone drop.** A row's top quarter means "above", its bottom
  quarter "below", its middle "inside"; the indicator is a heavy line
  on the edge or a heavy stroke round the whole row — Folio's binder
  vocabulary, which Scott already reads without being told. `motion`
  asks `can_move()` on every pointer move and returns no action when
  the drop would fail, so **a refusal is visible before release**
  rather than silent afterwards.
- **Sibling order is model state.** The edge zones forced it:
  `move(id, parent, index)`, an insert rather than an append, and
  reorder-within-one-parent as a legal move. The index a drop names is
  a PRE-removal position, which is the one subtlety in it.
- `EditorPane` — title entry, monospace markdown body, write-through on
  every keystroke, no save button and no dirty flag (there is nothing
  to be dirty against, and the store will own its own write policy).
  The status line carries the node's id and parent.
- Shell rebuilt around a `Paned` tree|note with a new-note button in
  the header. Cairn's demo pages are gone. Ctrl+N / Ctrl+Shift+N /
  Ctrl+Delete / Ctrl+L, plus Ctrl+Shift+D to dump the node tree.
- `protect` carried from Notr: no move, no edit, no delete — and a
  protected node anywhere under a cut vetoes the whole delete, because
  a lock a parent delete can route around is not a lock.
- 27 new selftest checks (57 pass / 0 fail, up from 30).

**The drop handler is the point.** Nothing in `TreePane_dnd.cpp`
touches a widget in response to a drop. It hands two ids to
`NodeSource::move()` and stops; the tree redraws because the model
announced a change. There is no widget move to get wrong, which is the
structural version of the lesson Notr teaches.

Not shipped, deliberately: persistence, markdown rendering, image drop,
libadwaita.

---

## Open decisions — blocking

**D1 — storage shape. ANSWERED (s003).** A folder of jots: flat
`notes/<uuid>.md`, `attachments/`, and a `jot.json` project file that
builds the tree. Original text kept below.

**D1 (original) — storage shape.** One document vs a folder of markdown files.
This is the whole project: it decides import/export, what "drop an
image in" does, whether the vault is git-able, whether other tools can
read the notes, and what a move means on disk. Everything else waits
on it.

**D2 — task representation. ANSWERED (s006, in conversation): tasks
are NODES.** The existing `core::Node` with optional task fields --
a node is a todo when it has them and a note when it does not. Not a
new entity, not a harvested line. See "Hot for next session" for the
fields, the reasoning, and the recommendation it overturned.

**D3 — libadwaita.** Adopt it for the modern GNOME look, or keep
hand-rolled GTK4 plus `Appearance.cpp`. Affects the shell layout work
directly, so it wants answering before the real UI starts.

**D4 — GTD state location. ANSWERED (s006, in conversation):
`jot.json`.** Task fields sit alongside the node fields already there;
a format version bump, and the note files on disk do not change at
all. Follows directly from D2 -- a field needs something with identity
to hang off, and a checkbox line has none.

Inline `#tags` remain a separate question and remain READ-ONLY: the
s006 drawer shows what a body says and commits to no storage location
for tags specifically.

**D5 — does Folio's markdown editor lift out? ANSWERED (s004): no,
because it does not exist.** Folio's `Editor` is ~13,000 lines across
`Editor.cpp` + `Editor_{build,format,text,views}.cpp` and it is a
**WYSIWYG rich-text editor**, not a markdown one. Its tags (`bold`,
`italic`, `justify_*`, `first_line_indent`) are applied by toolbar
commands and serialized to **HTML** by `EditorHtmlSerializer`; HTML is
the storage format of Folio's model.

Markdown appears in exactly two places, both at the file boundary:

- `Importer::parse_md` splits on ATX headings to build chapter
  structure, then hands the body to `plain_to_html`. **No inline
  parsing at all** — no emphasis, no links, no lists, no checkboxes.
- `Exporter::html_to_markdown` is a character scanner over stored HTML
  emitting `**` / `_` / `~~`. One direction only, and it escapes
  ``*_\`#`` out of prose rather than understanding it.

Neither is liftable into jot, and the reason is structural rather than
a matter of quality: **jot's storage format IS markdown**, so Folio's
entire HTML round-trip is precisely the layer jot exists without.

**What did lift, and it was worth the read:** the discipline around
`sp_auto_sense()`, Folio's screenplay element classifier. Restyle on
IDLE rather than per keystroke; a second re-entry guard beyond the
loading flag, which exists because the classifier's own edits clear
the first one mid-flight; and a rule that classification never
overrides something the user set deliberately. Roughly 150 lines of
hard-won shape, now in `EditorPane`. The markdown itself is ours:
`core/Markdown.{hpp,cpp}`, 400 lines, GTK-free, 39 selftest checks.

**D6 — Pole A or Pole B for the tree.** Pole A is GTK4's model-view
stack (`ListStore` → `TreeListModel` → selection model → `ListView` +
`SignalListItemFactory`), rows recycled through the factory lifecycle,
virtualised. Pole B is hand-rolled recursive `Gtk::Box` nesting with a
`Revealer` per group, one row widget per node, full rebuild on every
structural change.

**Folio chose Pole B** for its binder — arbitrary depth, multi-select,
inline rename, filter, drag-reorder — across roughly 350 sessions.
`SignalListItemFactory` appears in Folio only in dropdowns. An earlier
draft of this file told the next session to build a `TreeListModel`;
that was a reflex answer written before reading Folio, and it stands
corrected here rather than silently overwritten.

The invariant that makes Pole B correct is **the tree is small enough
that a full rebuild is cheap.** A few hundred nodes rebuild
imperceptibly; fifty thousand do not. So the question for jot is what
a large vault actually looks like. See Cairn's `docs/tree-and-dnd.md`
for the full distillation, including the DnD shapes worth lifting
either way.

**s002 built Pole B and measured it.** `JOT_STRESS=<n> ./build/jot`
loads a synthetic vault at startup instead of the fixtures, and every
rebuild logs its row count and milliseconds to the `tree` area:

| visible rows | rebuild |
|---|---|
| 11 | 0.7 ms |
| 500 | 18.7 ms |
| 2,000 | 76.3 ms |
| 5,000 | 153 ms |
| 12,000 | 363 ms |

Linear, ~30 µs per row; one 60 Hz frame is about 450 rows. Two caveats
that keep this honest: it is measured in the sandbox under software
rendering on x86-64, not on Scott's machine, and it times widget
CONSTRUCTION, not GTK's subsequent layout pass. The number that governs
is **expanded rows, not vault size** — a collapsed subtree emits
nothing.

Reading, not verdict: Pole B holds comfortably while what is on screen
stays in the hundreds, and degrades predictably and linearly past that.
D6 stays open until Scott has dragged something and formed the visual
half of the judgement.

---

## Requirements as stated (Scott, s001)

1. Markdown-forward notes and todos.
2. Drag and drop wherever possible.
3. Notes and todos together, OmniFocus-like but blending categories,
   notes and lists.
4. Modern GNOME look, with distinctive import/export.
5. Intuitive to GTD paradigms.
6. Images (and media) dragged into the markdown.
7. Every node can parent children — no folder/document split.
8. Links to related objects in the data structure.
9. Everything other than the data structure is named — buttons,
   widgets, lists — for debuggability.

Framing added in conversation: **it's a scribble pad.** Capture
first, file second. The app opens into a note, not a tree.

---

## s007 — the task model (done)

**Shipped: the fields, availability, `core::TaskIndex`, the drawer's
Task block, Notes/Today in the left pane, and the row context menu.**
`jot_selftest` reports **254 pass / 0 fail**, up from 196 -- 58 new
checks, and the section that carries them is the most load-bearing in
the harness.

### What D2 did not say, and had to be decided

D2 says a node is a todo when it HAS the task fields and a note when it
does not. **C++ has no absent `bool`**, and the commonest todo of all --
no dates, not flagged, not done -- is bit-identical to a note under any
inferred rule. So `is_task` is an explicit presence bit. The alternative
(a date being non-zero implies a task) would have made "make this a
todo" unrepresentable until you also gave it a deadline, which is the
toll gate in a smaller hat.

`Status` (Sequential / Parallel) is deliberately INDEPENDENT of
`is_task`: a plain note called "Taxes" is the natural container for a
sequence of steps, and requiring the container to be a todo would put a
checkbox on every project heading.

### One virtual, not six

`set_task(id, Task)` is the only new virtual on `NodeSource`;
`set_done`, `set_flagged`, `set_due`, `set_defer`, `set_status` and
`make_task` are non-virtual read-modify-writes on top of it. Six
virtuals would have been six chances for a store override to forget to
persist one of them, and `Project` has exactly one line to maintain.

`Change::Task` is its own kind rather than riding on `Flags`, because a
task edit changes what is AVAILABLE somewhere else entirely -- ticking a
step makes its sequential sibling the next action, three rows down and
possibly under a collapsed parent. `Flags` repaints a row; `Task`
re-asks the index.

### The engine, and the rules it needed that nobody had written down

- **defer inherits LATEST-wins, due inherits EARLIEST-wins.** A project
  deferred to next month defers its steps whatever they say; a project
  due Friday makes its steps due Friday whatever they say. The
  asymmetry is the point and it is tested in both directions.
- **A note among a sequence's children is not a blocker.** Only task
  children take part in the ordering. A reference note sitting in the
  middle of a sequential project cannot be "done", so blocking on it
  would stop the sequence dead at a row with nothing to tick.
- **A finished parent task takes its unfinished children with it.**
  Leaving the steps of a completed project available forever is the one
  wrong answer here that is loud rather than silent, and it is still
  wrong.
- **Sibling order really was already built.** The selftest now proves
  it: dragging the third step above the second makes it the next action.
  That is the whole of "drag to prioritise", and it needed zero code.

### Availability is derived, and that is why it is in core

It is a pure function of (parent status, sibling order, done, defer), so
it is a pure function under the selftest. `TaskIndex` caches MEMBERSHIP
in document order and **no verdicts at all** -- every query re-derives.
The check that earns the class its keep is the one LinkIndex earned in
s006: the incremental path must land exactly where a full rebuild lands.

The failure this guards is the worst one the app can have. **A task
wrongly hidden is a task you do not do**, and no surface would ever
report it -- the row simply is not there, and an absence looks like
having nothing to do.

### jot.json v2

Task fields written only when non-default, so a folder of notes looks
exactly as it did under v1 and a diff of `jot.json` shows the todos
rather than six false flags on every node. `status` goes to disk as a
WORD, because an enum's number is a fact about this build's declaration
order. **The note files on disk do not change at all**, and there is a
check asserting it.

### Today groups by parent, and shows the why

The group header is the parent's title; under it, **the first non-blank
non-heading line of the parent's note**. That line is the "why" the
report was asked for, and it is the thing OmniFocus cannot print because
a task's note there is a scratch field nobody fills in.

**Overdue is pinned above whatever view you are in**, ungrouped, and is
the one query NOT gated on availability: "you cannot start this yet and
it was due on Tuesday" is exactly the sentence you need to have read.

Three views only -- Today, Available, Flagged. Forecast and review are
perspectives over the same index and are worthless without a corpus.
**There is no Inbox**: a top-level todo groups under "Unfiled", which is
a state and not a place to empty.

### The left pane stopped being "the tree"

It is now HOW YOU NAVIGATE, with Notes and Today in a `Gtk::Stack`
behind one switcher. A Stack rather than swapping the Paned's child, so
the tree keeps its scroll position, collapse state and selection while
you are away. Three panes still hold; Today is not a fourth.

**F9 now hides the whole side**, not the tree inside it. That was very
nearly shipped wrong -- hiding `m_tree` would have left the tab bar
floating over an empty stack, and both-toggles-off is s006's whole
front door. The lesson generalises: **when a pane gains a sibling, every
existing verb that named the old child has to be re-read.**

### Two bugs the headless run caught that a compiler could not

1. **The context popover was parented to the `ListBox`.** `set_parent()`
   makes a popover a real widget child, `TreePane::rebuild()` clears the
   list with `while (get_first_child()) remove()`, and a ListBox refuses
   to remove a child that is not a row -- so the popover came back as the
   first child forever and the rebuild span. 1.7 million warnings in
   eight seconds. Parented to the pane's own Box instead, with the
   pointer coordinates translated into its space (they differ by the
   scroll offset, so a menu opened after scrolling would otherwise point
   above the row it belongs to).
2. The same run is what confirmed the numbers: on a fixture with eight
   todos, a sequential project, a deferred one and an overdue one, Today
   reported `overdue=1 rows=2` -- which is exactly the hand-computed
   answer. **That is still the trace channel.** Whether Today reads as a
   report or as a wall is Scott's, permanently.

### The call I made without an answer, now made

`- [ ] milk` in a body stays an INFORMAL checkbox that Today ignores.
Asked three times across the s006 conversation and not answered, so it
is my call, and it is now the shipped behaviour rather than a plan.
**The "promote to task" action is NOT built** -- it is in the backlog
and it is the reversible half. The reasoning stands: two todo systems
that both look real, where only one reaches the report, is how you stop
trusting the report.

### Deliberately not in s007

Repeating tasks (recurrence is where task apps go to become
complicated, and you cannot judge whether you need it until you have run
a week of flat ones), review intervals, and custom perspectives.

## s008 — capture (done)

**Shipped: the header capture line, `core::capture`, `jot --capture`
with single-instance forwarding, and the GNOME keybinding written down
in the README.** `jot_selftest` reports **268 pass / 0 fail**, up from
254.

ARCHITECTURE has called jot a scribble pad since s001 -- capture first,
file second -- and seven milestones later the shortest path to a new
thought was still Ctrl+N and then finding where the note went.

### The rule that makes it worth having

**Capture moves nothing.** No selection change, no editor load, no pane
switch, and the cursor stays in the box for the next thought. A capture
that took you somewhere would be a capture you learn not to use while
you are busy, which is the only time it matters.

The line is ALWAYS VISIBLE rather than behind a button, because a box
you have to summon costs a click and the premise is that capture costs
nothing. `Ctrl+Shift+Enter` reaches it; Escape gives the keyboard back
to the note.

### Nothing typed is ever lost

`core::capture_split` is the whole of what a captured line becomes, and
it is one function because the header box and `jot --capture` must not
disagree. Multi-line: first line titles it, the rest is the body. Short
single line: it is the title. **Long single line: the title is cut at a
word boundary and the body keeps the WHOLE line** -- a capture box
invites a sentence, a sentence makes a useless tree row, and truncating
without keeping the original would mean the app silently ate the end of
a thought.

### The confirmation lives in the placeholder

The tree rebuilds and the row appears, which is the real feedback -- but
in full-focus mode both side panes are hidden and nothing would show at
all. So the placeholder says what landed, and **the next keystroke is
what clears it**: a confirmation that never clears is a label, and one
on a timer is a race with how fast you type.

### Single instance, and no daemon

`HANDLES_COMMAND_LINE` plus GTK's default single-instance behaviour is
the entire mechanism: a second `jot` forwards its argv to the running
one over the session bus and exits. No autostart, no portal, no second
lifecycle. If jot is not running the command launches it, which is the
right answer anyway.

With text the window is deliberately NOT presented -- filing from a
script or a keybinding should not throw a window in front of what you
are doing. Without text there is nothing to file, so the only reading is
"let me type", and that does present.

### Two bugs, both found by running it, neither findable by reading it

1. **`on_activate()` was not idempotent.** Until `--capture` existed,
   activate() happened once per process and the override could afford to
   assume it. A forwarded command line calls activate() on the ALREADY
   RUNNING instance -- and that built a SECOND Shell: a second window, a
   second `core::Project` over the same jots folder, **two writers to
   one jot.json**, and every widget name duplicated. The registry warned
   about the duplicates before anything was lost, which is the registry
   doing precisely the job it exists to do.
2. **A registered GOption entry is REMOVED from the argv** that reaches
   `on_command_line`. Registering `--capture` so `--help` would list it
   is what made reading the flag out of argv find nothing, so every
   `jot --capture` silently became a plain activation. The flag now
   comes from the options dict (through the C API -- giomm's own header
   says the dict is unwrapped) and only the remaining words are read
   from argv.

   This was the second failure of the same thing, so it went to LOG MODE
   rather than a third guess: one `info` line reporting argc, remote and
   the parsed request, and the answer was in the first run --
   `argc=2 remote=true capture=false`. Remote forwarding had been working
   the whole time; the parse had not.

### Verified over a real session bus

`dbus-run-session` under xvfb, a primary instance holding a fixture jots
folder, then two `jot --capture` invocations from separate processes.
Both forwarded, both landed as top-level notes on disk, and
`jot --capture` with no text created nothing. **That is the trace
channel and it is an unusually complete one for this milestone** --
forwarding either happens or it does not. What it cannot tell you is
whether the box is in the right place in the header, or whether it
crowds the title on a narrow window.

## s009 — the desktop projection (done)

The mechanism the handoff assumed turned out to be right, and reading
the calendar server's source before writing a line of it was worth
more than the whole rest of the milestone's research.

### The split, and why it is where it is

`core/Projection.{hpp,cpp}` decides WHAT the desktop is told; `Desktop.cpp`
does the writing and is the only file in jot that includes libecal. The
decision is a pure function of the model and an instant, so it is under
the selftest; the writing can only be observed on a real session bus.
Everything that could be a decision instead of a side effect is one.

**What projects:** a todo, not done, with an EFFECTIVE due date.

**Not filtered by availability**, and that is the interesting call.
Blocked and deferred todos project. The card is a DAY GRID and answers
"what falls on this day"; a due date is a fact about a day whatever jot
thinks of the task's readiness, and availability is a question only
answerable next to the why, which is in Today. A projection that
quietly dropped rows would be a desktop that lies, and the lie would be
invisible from inside jot. So availability is a WORD in the
description, not a filter.

**Undated todos never project**, flagged or not. They have no day, and
inventing one would put jot's opinion on the desktop's grid.

### Three findings that only a run could produce

1. **DTSTART, not DUE.** `gnome-shell-calendar-server` takes a
   component's start time from DTSTART and gives up (start time 0,
   filtered out) when there is none. A VTODO carrying only DUE never
   reaches the card. jot writes both: DUE because that is what the
   format means, DTSTART because that is what the desktop reads.
2. **An all-day DUE equal to DTSTART is a zero-length interval**, and
   EDS's `occur-in-time-range?` does not match one against the day
   containing it. The symptom was perfect: a todo due TOMORROW appeared
   in today's window and a todo due TODAY did not. The end date is now
   exclusive, the same convention DTEND already uses.
3. **`wait_for_connected_seconds` is a timeout, not a budget.** The
   first connect in a cold session took the full thirty seconds,
   synchronously. Passing 0 is WORSE, not better -- the client comes
   back immediately and unusable and the first read then blocks with no
   timeout at all. The wait stays; the launch sync was moved out of the
   Shell's constructor and through the debounce instead, so the window
   is up before anything reaches EDS.

### The instrument

`jot_desktop_probe` writes a fixture projection through the real backend
and asks the list the SAME live query the calendar server asks. It went
through two rounds of being wrong in its own right, which is the part
worth keeping: the first version replicated only EDS's query filter and
disagreed with reality, because the calendar server RE-FILTERS
everything the query returns by DTSTART in `app_notify_events_added()`.
An instrument that models half a pipeline reports confidently on the
wrong half.

### The debounce is not polish

`refresh_tasks()` runs on every model change and a sync is a synchronous
D-Bus round trip. A change arms a 1500ms timer; the timer compares the
projection's FINGERPRINT (`projection_signature`, FNV-1a over exactly
the fields that reach the desktop) against what was last written, and
only a real difference crosses the boundary. Typing in a body changes
the model constantly and the fingerprint not at all. It is a fingerprint
of what WE LAST SENT, not a cache of the answer -- the projection is
recomputed from the model every time, so the worst a stale fingerprint
can do is skip a write that would have produced the same bytes.

### Deliberately not in s009

`Gio::Notification`. It rides along nearly free and was left out for the
same reason it was left out of s008: a milestone ends at a surface, and
this one already has one.

---

## s009b — the window remembers itself (done)

Width, height and maximized state persist in `core::Prefs`, alongside
the pane layout that was already there.

**Position is NOT stored, and cannot be.** GTK4 removed window
positioning outright -- there is no `gtk_window_move` -- and Wayland
gives an application no way to ask where it is or to place itself. The
compositor decides; jot says how big. This is not a deferred feature.

Two things the implementation had to get right, both measured:

- **`get_default_size()`, not `get_width()`.** The latter returns the
  content area while `set_default_size()` takes a size including
  decorations, so the round trip lost ten pixels each way EVERY LAUNCH.
  Four launch-close cycles made it obvious; one looked fine. See CANON.
- **A maximized window reports the SCREEN's size**, so while maximized
  the stored size is left alone -- it still holds what it was before, so
  un-maximizing restores the size actually chosen.

Recorded in the close handler, including on the path where the scratch
prompt CANCELS the close: a size learned then is still the right size.

---

## s010 — the projection, corrected (done)

**The calendar dropdown does not read task lists**, confirmed on Scott's
machine with `CALENDAR_SERVER_DEBUG=1`. Scott chose option A: write
VEVENTs into a `jot` CALENDAR source. A due date becomes an all-day
event -- a knowingly accepted semantic cost, and what every GTD app that
projects to a calendar does.

**Both clauses of the filter are load-bearing.** Read out of
`gnome-shell/src/calendar-server/calendar-sources.c` before writing a
line, because s009's headline failure was treating consistent-with as
verified:

```c
registry_watcher_filter_cb():
  e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR) &&
  e_source_selectable_get_selected (e_source_get_extension (...))
```

A calendar source that is not SELECTED is as invisible as a task list,
and nothing about it looks wrong from outside. Enabled matters too --
`ESourceRegistryWatcher` only offers the filter enabled sources.
`source_is_dressed()` checks all three on an existing source and
re-commits if any is missing.

- **The uid is `jot-calendar`, not `jot`.** A removal is not visible to
  the next line that asks: the registry proxy learns of it over D-Bus,
  so `ref_source("jot")` immediately after a successful remove can hand
  back the source just deleted. The display name is still `jot`, which
  is the only part a user sees. s009's task list is removed once on
  first connect, guarded by `e_source_get_removable()`.
- **`I_CAL_STATUS_NEEDSACTION` is a VTODO value** and was quietly wrong
  on a VEVENT. The property is gone; the rule already said it (done
  todos do not project).
- **The end must not equal the start for TIMED rows either.** s009 found
  the zero-length problem for all-day and fixed it as a special case;
  `ical_for(p.due + 1, p.all_day)` is now one rule for both. A deadline
  gets a one-second event -- jot does not invent a duration, it only has
  to be non-empty to be seen.
- **The connect is async, four round trips deep**, each step a member
  taking `void*` so no libecal type reaches `Desktop.hpp`. Not a thread:
  every call has an async form.
- **PENDING is a third state in `DesktopResult`** -- neither ok nor
  failed, work queued behind a connect, a second result to follow
  through `set_report()`. A queued sync is REPLACED rather than appended
  (the projection is a snapshot of the whole truth); a clear SUPERSEDES
  a queued sync; **`disconnect()` defers rather than cancels**, because
  untick during a first connect must still get the rows off the desktop.
- **The callbacks hold a weak box, not a raw `this`.** Four bare
  function pointers that can outlive the object, in exactly the window
  the async was introduced to survive.
- **`Shell::on_desktop_result()`** is the one landing place for every
  result, immediate or deferred, and the only place the status line's
  wording lives.

`jot_desktop_probe` is **14 pass / 0 fail**, up from 8: the original
eight unchanged in substance, plus five on the async path, because a
connect nothing exercises is a connect that does not work. It connects
through `Desktop::connect_blocking()`, which the app never calls.

`jot_selftest` unchanged at **290 pass / 0 fail** -- the core did not
move. Both halves of the `#ifdef` were built before shipping.

**Still never seen by eyes.** The claim the milestone rests on -- a jot
todo on its day in the GNOME calendar dropdown -- has not been observed
once, in either s009 or s010.

---

## Hot for next session

**s014 is Scott's call.** Capture is finished; what is queued behind it
has been queued for a while, and the two candidates pull different ways.

- **A receipt for notifications.** `g_application_send_notification()`
  returns void, which is what let a skipped send look like a delivered
  one for a whole evening in s012.
  `org.gtk.Notifications.AddNotification` is a real method with a real
  reply. Scott's idea and the right shape: **do not mark a deadline
  announced until delivery is acknowledged**, which turns the existing
  60-second tick into the retry with no retry machinery at all. Apple's
  local-notification API is the model -- it can read back what was
  actually delivered, which is the thing GTK lacks. This is the one that
  buys HONESTY rather than features.
- **The capture hotkey**, settable from jot's own preferences rather
  than by hand in GNOME Settings. Appending to
  `org.gnome.settings-daemon.plugins.media-keys` `custom-keybindings`,
  owning removal and key-already-taken. **There is still no preferences
  window** -- s012 put the residency toggle in the Today footer beside
  the other two, which buys time but does not answer the question. The
  `GlobalShortcuts` portal was never checked. With s013 done this is the
  feature that makes cold capture reachable without a terminal, which is
  an argument for taking it next.
- **The notification icon is the generic document, not jot's mark.**
  `jot-logo-symbolic` is not resolving from
  `~/.local/share/icons/hicolor/symbolic/apps`. Small, and visible in
  every screenshot.
- **"Mark done" as a notification action.** Cheap, and with residency on
  it is now the action most likely to arrive at a jot with no window.
- **The two s011 leftovers**: the zero-length calendar row (fifteen
  minutes instead of one second) and the untitled todo that projects
  (a RULE CHANGE -- needs Scott's answer, not an edit).
- **Images in the body, and the markdown view question.** Displaced
  since s007.
- **A due date needs a calendar widget.**

**Two milestones still have no visual half:** s007 and s008. The task
model, the drawer's Task block, Today, the tree's tick boxes, the row
context menu and the capture line have never been run by eyes.

**The postmortem on Optik / Quire / delr is still open.** Thirteen
milestones of "banked, not done."

---

## Displaced: images in the body

Was going to be s007. The scan half is finished -- `![label](path)`
comes out of `core::scan` as a `Link` with `image == true` and a range
that includes the leading bang -- and `attachments/` has existed since
s003 with nothing written into it. What remains is the drop handler,
the filename question (its own name collides with the next screenshot;
a uuid never collides and never means anything in a file manager), and
the rendering decision, which is the expensive one: an inline image is
the first thing to break the s004 invariant that the buffer is
byte-identical to the file on disk.

Also displaced, and related: **the markdown view question.** "Formatted
and the markdown code" can mean a source toggle (cheap, a day),
side-by-side (medium), or true marks-hidden rendering (expensive --
`TextMap` enters the editor and there is a second coordinate space).
Scott has not said which. The source toggle is worth building first
regardless.

---

## s006 — the metadata drawer (done)

Tree | note | drawer as nested `Paned`, lifted from Folio flags and
all, with F9/F10 toggles as stateful actions. Both off is the focus
mode, and that is the feature rather than a side effect.

`core::LinkIndex` (one pass at load, incremental on body change),
`core::Prefs` (the layout pump, second pump on the Recents shape), and
`core::Scan` extended to carry real `Link` and `Tag` records — s004 had
been throwing the link target away because styling never needed it.

The status line retired: its parent id, child count and protected state
are in the drawer now, with the parent's TITLE instead of its id. A lock
icon on the title entry covers the one thing the drawer could not take
over, because the drawer can be hidden and protection changes what the
editor lets you do.

`set_level()` finally has a consumer: `JOT_DEBUG=drawer,tree`.

Added after Scott ran it: rename three ways (double-click a tree row,
a Name field at the top of the drawer, the editor's title entry), all
routed through `NodeSource::set_title` with the model as the only
channel between them; and Save / Save as..., the latter writing a
second jots folder via `Project::adopt()` and switching to it.

Selftest 156 -> 196. Two dead files from s005 (`FirstRunDialog`,
`core/Vault`, ~800 lines unreferenced by the build) deleted.

---

**Superseded: s004 — the editor made real.** Gated on **D5: does Folio's markdown
editor lift out?** — which is answerable in an hour with Folio's
editor surface and its buffer/tag handling in front of us, and is
guesswork without them. Whatever the answer, the milestone ends at a
surface: a body pane that styles markdown as you type rather than
showing it as source.

Also queued, smaller: a saved/unsaved indicator in the status line
(offered, not yet asked for), image drop into the body (now
unblocked — `attachments/` exists), and multi-select drag.

---

**Superseded: s003 — persistence behind the same seam.** The swap is the test:
implement `core::NodeSource` against whatever D1 decides, construct it
in `Shell::build_ui()` in place of `MemoryNodes`, and **change nothing
else.** If `TreePane` or `EditorPane` has to change by a line, the seam
was drawn in the wrong place — which is a finding, and a cheap one.

It is gated on D1, and D1 is now a looking question rather than an
argument: open the app, drag a subtree, watch the id in the status
line, and ask what that move should mean on disk.

The milestone ends at a surface like every other one: the visible half
is that the fixtures are gone, the tree comes up from a file, and what
you typed last time is still there.

---

**Superseded plan from s001, recorded so the change is visible.** This section
previously read: node model and its pump first, headless, with the
tree view and DnD after. That order would have ended at least one
milestone with nothing on screen — which CANON's new *A milestone ends
at a surface* now says is a milestone that can't be called done, since
only the trace channel would have validated it. The bridge-milestone
pattern was being used as a default order rather than as the narrow
blocked-below exception it's written for.

**The replacement, and it unblocks D1 rather than waiting on it:**

**s002 — the surface, on fixtures.** A window with the real shapes: a
tree on the left, an editor pane on the right, a node title, room for
the capture path. Backed by a node struct with five or so fields and
**no persistence at all** — the tree is populated from a hardcoded
fixture set. Dragging a row rewrites `parent_id` and the tree redraws
from the model. Crude and throwaway on purpose.

The discipline that keeps this from becoming Notr: **fixtures arrive
through the same interface the real store will implement.** The view
reads from a source abstraction, not from a `std::vector`. When
persistence lands, the source is swapped and the surface doesn't
change by a line. If it has to change, the seam was drawn wrong —
which is a finding, cheaply.

What this buys: D1, D2, D3 and D6 all become answerable by looking
instead of by argument. What a node's row needs decides what a node
is. Whether checkbox-lines-as-tasks feels right is visible the moment
a body pane exists. Whether Pole B's full-rebuild is fast enough is a
measurement once there's something rebuilding.

**Then, in order, each ending at a surface:** persistence swapped in
behind the source (D1 answered by then); the editor made real; the
GTD query views.

**DnD reparent rides in s002**, not later. It's the defect that most
defined Notr, it's one field write in this model, and it is exactly
the kind of thing you only judge by dragging.

---

## Backlog

- Notr's `.ntr` reader, for importing the existing notes. Format is a
  flat sequence of `wxDataOutputStream` records — title, desc, uuid,
  parentUUID, text, rgba, protected — with no version field and
  `while (istrm.CanRead())` as the read loop. Write it as a one-way
  importer into jot's own format; do not adopt the format.
- Model-side `dump()` — node tree with ids and link targets, the
  sibling of `registry::dump()`.
- Carry Notr's per-node `protect` flag; it's unusual and genuinely
  useful.
- Multi-select drag (Folio has it; s002 moves one node at a time).
- Auto-expand a collapsed row when a drag hovers over it.
- Images and media dropped into the body (requirement 6). Waits on D1:
  where the bytes go is the storage question.
- Marks-hidden live preview (the Obsidian variant). s004 built the
  marks-visible half, which is the same styling plus nothing; hiding
  is this plus a reveal rule, and it drags `TextMap` in because the
  buffer stops being byte-identical to the file. Deliberately not
  built; nothing in s004 has to be undone to build it.
- The checkbox hit area is the three characters of `[ ]`, which in a
  proportional font is a small target. A wider area steals ordinary
  cursor placement on task lines, so this wants looking at rather
  than widening on principle.
- Inline styles do not nest: `**bold with _italic_ inside**` styles
  the bold and leaves the underscores plain. Fixing it means a
  delimiter stack, which is where a markdown scanner stops being 400
  lines.
- Comment prose in the transplanted spine still reads in
  "seed"/"paradigm" voice in places. Rewrite each surface's header
  block as that surface gets replaced, rather than in one sweep.
- Decide the app id's final case (`io.github.scott8420.Jot` today;
  lowercase `.jot` is equally valid and matches the app name).
- Search across all notes. Notr's find/replace only ever saw the open
  note.
- The EXDEV branch of `relocate_jots()` is tested for its refusals and
  for success on ONE filesystem. A real cross-mount move (USB stick,
  second drive) has not been run. First suspect if a relocate ever
  misbehaves.
- **Capture that knows it is a todo.** Today a captured line is always a
  note. A leading `[]` or a trailing `!` are the obvious syntaxes and
  both are guesses; the question is answerable by using capture for a
  fortnight and noticing what you actually type.
- **The capture line's width on a narrow window.** It is 16 chars
  preferred, 28 max, packed at the start next to the logo, the new-note
  button and the tree toggle. Whether that crowds the jots title is a
  looking question.
- **"Promote to task" on a `- [ ] ` line in a body.** s007 shipped the
  decision (informal checkboxes stay informal and Today ignores them)
  without the escape hatch. The line has a range from `core::scan`
  already; what it needs is a verb that makes a child node out of it and
  removes the line. Until it exists, the only way to turn a scribbled
  checkbox into a real todo is to retype it.
- **A due date needs a calendar.** s007's date fields are text
  (`YYYY-MM-DD`, `today`, `tomorrow`), which is fast to type and awful
  to guess at. A `Gtk::Calendar` in a popover next to each entry is the
  obvious fix and was left out to keep the milestone at one surface.
- ~~The notify status line counts KEYS, not sends~~ -- **fixed in
  s012.** It reports sends with a clock time on them. The general shape
  is worth keeping: a status line for a feature delivered by ANOTHER
  PROCESS has to say WHEN, or a thing that fired at launch is
  indistinguishable from a thing that fired on time.
- **jot is NOT listed under Background Apps, confirmed by looking.** A
  flatpak in the same session is. The footer's residency line still
  tells the user jot can be quit from that menu -- **which is now wrong
  and should be reworded.** Whether
  `org.freedesktop.portal.Background.SetStatus` would help an
  unsandboxed app is unknown.
- **Nothing tests that a resident jot still NOTIFIES.** The selftest
  cannot reach it -- there is no window in a headless run to hide. This
  is exactly where the s012 bug lived, undetectable by any automated
  check jot has. Convergent evidence for this one is permanently
  Scott's.
- **A notification has no receipt.** `send_notification` returns void.
  `org.gtk.Notifications.AddNotification` has a real reply; jot should
  call it directly and **not mark a deadline announced until delivery is
  acknowledged**, which makes the existing 60s tick the retry. Scott's
  idea, modelled on Apple's local notifications, which can read back
  what was actually delivered.
- **A tree rebuild per keystroke** -- twenty in fifteen seconds, 4-7ms
  each, in the s012 log. Nothing wrong; the first real trace of Pole B's
  cost, and data for D6.
- **A drained capture's `created` is the DRAIN time**, not the time it
  was captured. The real one is in the spool file's front matter and
  nowhere else. Needs a `NodeSource` seam change (`set_created`) or a
  variant of `core::capture` that takes an instant.
- **A timed row renders as a zero-length appointment** -- "4:12 PM -
  4:12 PM" in the calendar dropdown, because a timed deadline gets a
  one-second event (s010, to clear EDS's zero-length filter) and GNOME
  renders to the minute. Fifteen minutes would read normally and the
  filter would not care.
- **An untitled todo still projects**, and "Untitled" turned up on the
  desktop beside real tasks. A rule change in `core::project`, not a bug
  fix, so it needs deciding.
- **"Mark done" as a notification action.** Cheap, and the strongest
  argument notifications have over a calendar row -- but a write path
  from the tray into a model that may have moved since it was sent. Left
  out of s011 on purpose.
- **The build-tree path is baked into the installed `.desktop` Exec.**
  Move or rebuild the tree elsewhere and notifications stop working with
  no error. Re-run `./install-desktop.sh`.
- **The probe needs the EDS DAEMON, not just the dev headers.** A fresh
  sandbox has `libecal2.0-dev` and no `evolution-data-server`, and the
  symptom is `Failed to execute program
  org.gnome.evolution.dataserver.Sources5`. Belongs in a setup script.
- **Scott's machine is GTK 4.22.4 / Fedora 44 / aarch64**; the sandbox is
  GTK 4.14.5 on x86-64. Two version gaps and an architecture gap. The
  `get_parent` criticals were invisible here and obvious there.
- Other panes can still destroy widgets during a gesture. The tree is
  fixed; the drawer's link ListBox rebuilds on `refresh()` and has not
  been audited for the same shape.
- **Build both halves of the desktop #ifdef before shipping.**
  `cmake -B build-noecal -DJOT_DESKTOP=OFF && cmake --build build-noecal`.
  s009 shipped a fallback branch nothing had ever compiled; see CANON.
- **`kAreaCount` in Log.cpp is hand-kept.** s009 added a `desktop` area
  and had to bump it; a new area added without bumping it is a logger
  that never gets built. Deriving it from the enum is a five-minute fix
  nobody has done.
- **The desktop status line has never been seen.** It is the only
  feedback for a feature whose effect happens in another process, and
  whether it says enough is a looking question. s010 added a
  "Connecting to the desktop..." state to it that nobody has watched
  resolve.
- **The legacy task-list removal runs ONCE per machine and the probe
  does not check it.** Verified by hand in the sandbox (a planted
  s009-shaped `jot.source` was gone after one run); on Scott's machine
  it is a first-connect side effect with no instrument on it.
- Tags are READ-ONLY (s006). Making one clickable -- filter the tree to
  everything carrying it -- is the visible half of whatever D4 answers,
  and is the natural first move once it is answered.
- The 50ms paned-position scar lifted from Folio in s006 is empirical,
  not understood. INVESTIGATE, do not tidy: first suspect if a pane
  ever comes back as a sliver after being hidden.
- A global tag list (every tag in the jots folder, with counts). The
  drawer shows one note's tags; the index shape that would answer
  "everything tagged #idea" does not exist yet and is D4-gated.
- **Does jot reopen the most recent jots folder at launch, or come up
  on the scratch buffer?** It reopens, today. That is not a default --
  nothing creates or suggests a folder -- but it does mean a fresh
  launch never reaches the no-folder front door s005 built. Raised by
  Scott (s006), unanswered.
- **The header's jots-folder name does not look renameable.** It has
  now been read as a leftover string twice (s005, s006) by the person
  who built it. Nothing is wrong with what it says; the affordance for
  changing it is two levels into a menu. A third occurrence makes this
  a finding rather than a note.
- Rename has no undo, like every other structural change. When
  structural undo lands, rename is the easiest first verb to carry.
- Structural undo — node delete, move, protect. Notr had undo only on
  the text control, which is the smaller half.
