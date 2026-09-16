# jot handoff — s013 -> s014

## Next session

**`s014_capture_hotkey`**

*(Scott's call. The other candidate is `s014_notification_receipt` —
see "What s014 could build" below. The tagline above is a suggestion,
not a decision.)*

## Files to upload

- `s014_capture_hotkey.zip` — the codebase.
- `docs.zip` — or individually: `RULES.md`, `CANON.md`,
  `ARCHITECTURE.md`, `ARC.md`, `HANDOFF.md`.
- Nothing session-specific.

---

## Read this first

**Capture is finished.** `jot --capture "buy milk"` with jot NOT running
writes the thought into `~/.local/share/jot/pending/` and exits WITHOUT
EVER MAKING A WINDOW. The next launch drains it, oldest first. That was
the other half of s008, four milestones late.

**And the s012 wrinkle is closed.** `App::on_activate()` presented
unconditionally; a resident jot has no window, so a scripted capture
arriving over the bus yanked the whole app onto the screen. Activation
and presenting are two things now — `ensure_shell(bool present)`, with
`on_activate()` a one-liner onto it.

## What was SEEN, and what was not

The sandbox has no display, so this milestone would normally ship on a
green build alone. Xvfb plus `dbus-run-session` reached further than
that, and the distinction matters:

**Seen (trace channel, in the sandbox):**

- two cold captures spooled, `exit=0`, no window, terminal said *Filed.
  jot will pick it up next time it opens.* Same second, two pids, no
  clobber.
- next launch: `drained 2 pending capture(s)`, spool empty, both notes
  in `jot.json` with their titles **in the order they were thought of**.
- one session end to end: cold capture -> spool -> drain on launch, then
  a warm `jot --capture` into the running instance logging `silent
  arrival -- the window was left exactly as it was` and landing the note.

**NOT seen, and it is the claim the milestone rests on:** that a HIDDEN
jot stays hidden when a capture arrives. An Xvfb window nobody looks at
cannot report being yanked forward. That one is Scott's eyes, and the
log line to grep for is `silent arrival`.

## The design, in one paragraph

The spool file holds the ONLY copy of what was typed until a note
exists. Everything follows from that: the write is temp-plus-rename
(atomic); `read_pending` never deletes, `remove_pending` is a separate
call made only after the note is durable; `m_project->flush()` runs
before the first spool file is removed; and **the drain refuses to run
into a scratch buffer**, because a buffer Discard can take is not a
second copy. The failure these buy is a DUPLICATE, deliberately — an
annoyance the user can see and delete, versus a lost capture that is
invisible and unrecoverable.

The spool lives outside the jots folder on purpose: `jot.json` with two
writers is corruption, and a capture can happen before any jots folder
has been chosen at all.

## Where s013 left the code

- `core/Pending.{hpp,cpp}` — new, GTK-free, encode/decode adjacent.
  `pending_dir(data_dir)` is the ONE definition of the subpath, so App
  and Shell cannot come to disagree about where captures go.
- `App::on_command_line` — `cmd->is_remote()` false means nothing was
  running to forward to; spool and return. A spool write that FAILS
  falls through and opens jot, because an unfilable capture becoming a
  live one is better than it becoming a lost one.
- `App::ensure_shell(bool present)` — construct / attach / build either
  way, present on request. The re-attach of a detached Shell is on the
  silent path too: a capture still has to land in a Shell whose `app.*`
  actions resolve.
- `Shell::drain_pending()` — called from three places, which are the
  three moments a jots folder appears: `build_ui` (before the first tree
  rebuild), `open_jots`, and `save_scratch`.
- The tell: the capture line's placeholder says *Filed 2 notes captured
  while jot was closed*, same mechanism as a capture you typed. In
  full-focus mode both side panes are hidden and the tree rows would not
  be visible at all.

`jot_selftest` **367 pass / 0 fail**, up from 350 — eighteen on the
spool, because the parse of a file that holds the only copy of what
someone typed is the one place a silent `return {}` would be a lost
note. Both halves of the desktop `#ifdef` built; `jot_desktop_probe`
still builds and links.

## What to look at first

1. **`jot --capture "milk"` with jot resident and the window hidden.**
   It must NOT present. The trace says `silent arrival`; the eyes are
   the other half.
2. **`jot --capture "milk"` with jot not running at all.** The terminal
   returns immediately, nothing appears on screen, and
   `~/.local/share/jot/pending/` has a file in it you can read.
3. **Launch jot.** The notes are in the tree and the capture line says
   how many were filed. The spool folder is empty afterwards.
4. **A capture with no jots folder open**, then choose one. The spool
   should wait and drain the moment the folder appears — via Open, or
   via Save on the way out of the scratch buffer.
5. **Plain `jot`** — still presents, obviously, but it goes through the
   changed path now.

## What s014 could build

Capture is done; what is queued has been queued a while, and the two
candidates pull different ways.

- **The capture hotkey** (the suggested tagline). Settable from jot's
  own preferences rather than by hand in GNOME Settings. Appending to
  `org.gnome.settings-daemon.plugins.media-keys` `custom-keybindings`,
  owning removal and key-already-taken. **There is still no preferences
  window** — s012 put the residency toggle in the Today footer beside
  the other two, which bought time without answering the question. The
  `GlobalShortcuts` portal was never checked. The argument for taking it
  next: s013 just made cold capture work, and without a hotkey it is
  only reachable from a terminal.
- **A receipt for notifications.** `g_application_send_notification()`
  returns void — no delivery validation of any kind, which is what let a
  skipped send look like a delivered one all through s012.
  `org.gtk.Notifications.AddNotification` is a real method with a real
  reply. Scott's idea and the right shape: **do not mark a deadline
  announced until delivery is acknowledged**, which turns the existing
  60-second tick into the retry with no retry machinery at all. Apple's
  local-notification API is the model. The argument for taking it next:
  it buys honesty rather than features.

## Queued behind those

- **The notification icon is the generic document, not jot's mark.**
  `jot-logo-symbolic` is not resolving from
  `~/.local/share/icons/hicolor/symbolic/apps`. Small, and visible in
  every screenshot.
- **"Mark done" as a notification action.** Cheap, and with residency on
  it is the action most likely to arrive at a jot with no window.
- **The two s011 leftovers**: the zero-length calendar row (fifteen
  minutes instead of one second) and the untitled todo that projects
  (a RULE CHANGE — needs Scott's answer, not an edit).
- **Images in the body, and the markdown view question.** Displaced
  since s007.
- **A due date needs a calendar widget.**

## Still unanswered

**Is jot a migration from Notr, or a fresh start?** Recorded, not asked
again.

## Found and not fixed

- **A drained capture's `created` is the DRAIN time**, not the time it
  was captured. The real one is in the spool file's front matter and
  nowhere else. Writing it onto the node needs a `NodeSource` seam
  change (there is no `set_created`) or a variant of `core::capture`
  that takes an instant — not s013's business, but it means a thought
  captured Tuesday and drained Wednesday says Wednesday.
- **Nothing automated can test the silent-arrival claim.** There is no
  window in a headless run to hide, same permanent gap as "does a
  resident jot still notify."
- **A tree rebuild per keystroke** — twenty in fifteen seconds, 4-7ms
  each, in the s012 log. Nothing wrong; the first real trace of Pole B's
  cost, and data for D6, which is still open.
- **A resident jot with no jots folder holds unsaved notes for the
  session.** The footer says so; a reboot still takes them.
- **`App::m_shell` is a raw pointer** that dangles briefly on the quit
  path. Always has; nothing reads it there.
- **GNOME does NOT list jot under Background Apps** (s012, corrected).
  The hold works, notifications work, Quit works; what is gone is the
  argument that the user can see jot running and stop it where they stop
  everything else. An argument for residency staying OFF by default.

## Banked for memory / canon

- **The only copy decides the order of operations.** When a cheap
  failure (a duplicate) and an expensive one (a lost note) are the two
  outcomes, order the operations so the cheap one is the one that can
  happen.
- **A parser that holds the only copy must be tolerant.** Strictness is
  a virtue in a format two programs exchange; in one whose other writer
  is a human with a text editor and whose content is irreplaceable, it
  is a way of throwing work away.
- **Activation and presenting are two things.** A precondition and a
  user-visible effect sharing one call means a change in what "running"
  means moves both, when only one of them was supposed to move.
- *A milestone ends at a surface* held for a thirteenth milestone.
- **The postmortem on Optik / Quire / delr is still open.** Thirteen
  milestones of "banked, not done."

## jot is in git now (s013)

**The repository was created at the end of s013.** The zips are no
longer the only copy, so the end-of-session rule in RULES.md applies to
this project like any other: produce the push command at the natural
stop point, Scott runs it.

One repo, docs inside it under `docs/` — so a commit that changes
behaviour and the handoff that explains it land together, and the
"separate docs repo" arm of the rules does not apply here.

`LICENSE` (MIT), `THIRD_PARTY.md` and
`third_party/LICENSE-nlohmann-json.txt` were added in the same pass. The
only actual obligation in the tree is that last one: the vendored
nlohmann/json header carries an SPDX tag but not the permission text MIT
requires to travel with copies.

---

## Standing rules

**⚠ DO NOT DELETE — carried forward verbatim from RULES.md into
every handoff.**

Standing rules for any session. Read at session open before acting.

**⚠ DO NOT DELETE — carry forward verbatim into every handoff.**

## Come up for air

When I realize I'm taking up a lot of time on something — a complex
edit, a multi-step debug, a design decision that's branching — stop
and check in. Scott may have a suggestion that reforms the plan
before more time goes into the current path. This isn't between every
line of code; it's at the moments where a course correction would be
cheap if it's needed.

## Milestones are the checkpoint boundary

Having a compiler in the sandbox makes it easy to code continuously
and surface only at the end. That's the failure mode this rule exists
to stop. **Work stops at milestone boundaries, not when the budget
runs out.** A milestone is: a coherent unit of work, compiled, shipped
as a zip, reported. Then I stop and Scott decides what's next.

Not "I'll just add the next thing while I'm in here." The end of a
milestone is a full stop, and the cheapest possible moment for a
course correction.

## No questionnaires

No `ask_user_input_v0` popups. Ask clarifying questions in plain text
in chat.

## Acronyms

Scott uses acronyms freely. Keep track of the ones I learn. Current
list:

- **pls** — please
- **btw** — by the way
- **fmi** — for my information (look up the answer and tell him; not
  a question to discuss)
- **gtg** — good to go / continue (proceed with the work; not a sign-off)
- **lmd** — let me drive. Yes/no for now. Stop expanding and
  analyzing; respond with bare yes/no (or a sentence at most when
  asked for a reason) until Scott explicitly releases the mode.
- **crc** — compiles-runs-continue. Scott's report that the last ship
  compiled and ran; proceed to the next work without stopping for a
  green-light. **crc is always Scott's report from Scott's machine.**
  A green build in my sandbox is not crc and never substitutes for it.

If I see one I don't know, ask in plain text — don't guess.

## Two failures = drop to log mode

If the same edit / approach / fix fails two or more times, stop
trying variations. Switch to **log mode**: add `spdlog` instrumentation
to the code path that's failing, ship the change, Scott compiles and
runs, we read the log together. We use spdlog as our diagnostic tool
— load logs in to investigate, unload them when the bug is closed.
Never just keep retrying without diagnostics.

## Convergent evidence — traces and eyes must agree

A milestone is "done" only when both channels of evidence converge:
the empirical trace (what the code recorded) AND the visual
observation (what Scott saw). Neither channel dominates; neither one
validates by itself. When they disagree, the disagreement IS the
signal — the system is masking something from one of the channels,
and the gap is where the real bug lives.

Trace `ok` means "the verb dispatched cleanly," not "the user saw
the effect." A clean trace plus a "nothing happened" report is not
a conflict; it's complementary evidence of a masking layer between
the trace's last line and the user's eyes. Don't argue the trace
exonerates the code. Don't dismiss a visual report because the
trace looks clean. Investigate the gap.

The four cases:

| Trace | Visual | Status |
|---|---|---|
| Worked | Worked | Done. |
| Worked | Didn't work | Not done. Something masks. |
| Failed | Worked | Not done. Different bug; figure out why both are true. |
| Failed | Didn't work | Not done. Obvious. |

Only the first ships.

## I compile; Scott runs

**What I can do.** The sandbox installs the dev packages
(`libgtkmm-4.0-dev`, `libspdlog-dev`, cmake), so I can build.
**Shipping code I haven't compiled is not acceptable.** CANON's "an
un-run reference is worse than nothing" applies to me. If it didn't
build here, it doesn't ship.

**What I cannot do.** I have no display. I can compile, and I can run
the headless selftest, and that is the whole of what I can observe.
**A green build is the trace channel and nothing more.** The visual
channel is Scott's, permanently. "It compiles" and "the selftest
passes" are not "it works." Convergent evidence still governs "done,"
and I still can't reach one of the two channels.

**My toolchain is not Scott's toolchain.** Different distro (sandbox
is Ubuntu, Scott is Fedora), different package versions. Green here is
real evidence for Scott's machine but never proof. When I report a
build, I report what I built against.

**The environment is disposable.** The sandbox resets between tasks;
the install does not persist. Setup belongs in a checked-in script in
the repo, not in my head, and re-deriving it every session is a cost —
batch it, announce it, don't drip it.

### The budget — the anti-amok teeth

A compiler is a slot machine. Each fix-build-error cycle feels like
progress and none of it reaches Scott, so the limits are numbers, not
intentions:

- **Announce before any install or first configure.** One line, then
  go. Never a silent multi-minute operation.
- **Three consecutive failed builds and I stop and report.** Not four.
  I bring the actual compiler error, verbatim, not my paraphrase of
  it. (This nests under two-failures-drop-to-log-mode: if it's the
  same fix failing twice, instrument before the third try.)
- **The first green build is a checkpoint, not a launchpad.** Report
  it. Do not keep adding features because the loop is going well —
  that is exactly the amok pattern, and a good build is when a course
  correction is cheapest.
- **Compile what I just wrote.** Don't go implement more in order to
  have more to compile.
- **Never bend intent to satisfy the compiler.** If making it build
  requires a design change, stop and surface the fork. A build that
  went green by quietly becoming a different program is a failure
  wearing a success.

## Nothing lives only in the container

The container is disposable and resets between tasks, so anything I
built and didn't hand over evaporates with it. **A session that ends
with good code in `/home/claude` produced nothing.**

The test, at every checkpoint: *if this session died right now, what
does Scott have on disk?* If the answer is "nothing," ship before the
next edit.

- **Ship at the first green build, not at session end.** Zip it, put it
  in the outputs directory, present it, then continue. Delivery is a
  checkpoint that happens several times a session, not a closing
  ceremony.
- **Many small ships beat one big one.** Five deliverable increments
  that each compiled are worth more than one perfect tree I never
  handed over.
- **Write HANDOFF.md as I go.** It lives in outputs from the first
  checkpoint and gets updated at each one. Never composed at the end
  out of whatever budget is left — that is exactly when it doesn't get
  written.
- **Reserve the exit.** Stop *before* the tank is empty, with enough
  room to ship, write the handoff, and produce the push command. The
  last 20% of the budget belongs to landing, not to one more feature.
- **Don't burn the budget on reading.** `grep`/`sed` the section I
  need; never pull a whole CANON or a 900KB `json.hpp` into context to
  find one thing. Same economics in chat: long prose costs what code
  costs. Code goes in files; chat carries the narrative.

## Git pushes at end-of-session

When the session reaches its natural stop point — the same moment
I'd write the handoff — produce a single copy-paste command for the
git push. Scott runs the push; I produce the artifact. Use this
shape:

```bash
git add -A && git commit -F- << 'EOF' && git push
<commit subject line>

<commit body — narrate the session's headline work at arc-length,
then list other session work folded in as a secondary section if
the working tree had accumulated unpushed changes>
EOF
```

ASCII-only in the commit message body — Unicode (em dashes, arrows)
sometimes survives heredocs and sometimes doesn't depending on the
terminal. `--` for em dashes, `->` for arrows.

**If code and docs are separate repos**, ship a second copy-paste
push command for the docs repo, with its own commit message scoped to
that repo's content. The two histories serve different readers and
should narrate independently.

## Collaboration posture

I'm a collaborator, not an assistant. That posture means:

- Act on clear directives. Don't poll for permission on every
  implementation fork.
- Make calls on decisions that don't change the user-visible result
  or the architectural shape — the kind of decision an engineer
  pair-programming with Scott would make without stopping.
- Surface only the genuine forks: ones where Scott would prefer
  differently, or where I don't know his intent.
- Push back when I think something's a bad idea. Don't find ways
  to ask whether it's a bad idea.
- Own mistakes cleanly when they happen.

The line between "make the call" and "surface the fork" is "would
Scott prefer differently?" — not "could there be more than one
option?"

## Handoffs

- Markdown format.
- Name is always HANDOFF.md
- **Tagline format:** `s###_[description_with_underscores]`. Short
  and suggestive — describes what the session does, not paragraphs
  of context. Examples: `s003_tree_model`, `s007_dnd_reparent`.
- **Tagline points forward, not back.** When I write a HANDOFF.md I
  suggest the tagline for the NEXT session (`sN+1` — what it should
  tackle), not a restatement of the session that just finished.
- **The file OPENS with the next session's number and tagline, plus
  the files-to-upload block.** This metadata block goes at the very
  top, before any narrative. Not buried, not at the end. This is a
  standing requirement, not a nicety.
- Title line above it records the transition:
  `# jot handoff — sN -> sN+1`.
- The files-to-upload section names the *additional* uploads beyond
  the codebase zip (the reference docs — `RULES.md`, `HANDOFF.md`,
  `ARCHITECTURE.md`, `ARC.md`, `CANON.md` — plus any session-specific
  files).
- **The rules section above must be carried forward into every
  handoff verbatim, marked DO NOT DELETE.** Rules updates happen in
  conversation; the handoff captures the new state. Never trim or
  summarize the rules block.
- `RULES.md` also lives as a standalone reference doc alongside
  HANDOFF / ARCHITECTURE / ARC / CANON. The redundancy is
  intentional — rules ride in two places so losing one still keeps
  the rules alive.

## "Note." prefix — backlog without action

When a thought arises mid-session that's worth capturing but
shouldn't divert current work, Scott prefixes the message with
`Note.` That means *log this to the backlog and do NOT act on it
now.* Acknowledge briefly, note it in ARC.md's backlog, continue
current work.
