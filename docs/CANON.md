# CANON.md — what we believe about building well

The body of beliefs carried forward from Curvz, Folio, Optik, Slate,
Plunk, Quire and delr. Each principle earned its place by failing
painfully when ignored.

**Provenance note.** Curvz's CANON is the long form — hundreds of
pages with the full worked example behind every rule. This is the
*portable* subset: the entries that apply to any project, stated
tightly. Where an entry names a Curvz session (s191, s203) it's a
pointer into that long form, not something you need to have read.
Entries that were specifically about Curvz's vector model (the three
coordinate spaces) or its scripting substrate (RunContext, headless
verb singletons, row-bound Scriptables) are deliberately absent —
jot doesn't have those surfaces. If jot grows a scripting layer,
port them then, not now.

This is canon — authoritative, durable, foundational — not gospel.
Beliefs revise when reality argues against them.

---

## The frame: structurally enforced discipline

> Make the right thing the easy thing and the wrong thing impossible.

Every "must remember to" rule is a future bug. Vigilance fails over
hundreds of sessions. Structure persists.

The unifying principle behind every rule below: **don't trust
yourself to maintain a discipline by attention; arrange the code so
the discipline is enforced by construction.** UUIDs over counters
(unique by construction, nobody checks for collisions). Encode/decode
pairs in the same file (changing one forces seeing the other).
Compile-time errors over runtime checks (the linker is the oracle).
A separator that can't appear in user input (presence of the
separator *is* the validation).

When you find yourself writing "remember to X" in a comment or a
handoff, that's the signal: there's a structural fix waiting that
makes the X automatic. The comment is a placeholder for not having
done the structural work yet.

This frame ranks above every specific rule below.

---

## Vocabulary first, file layout second

When a file or a codebase gets painful, the question isn't "where do
I cut?" It's "what categories of code does this contain?"

The vocabulary is the load-bearing decision; the file layout is its
expression. Let the code argue with the layout when it doesn't fit;
adjust the layout, keep the vocabulary.

**Test for vocabulary maturity:** can you answer "where does X live?"
deterministically from the vocabulary, without grepping? If yes,
the vocabulary is right. If no, you haven't found the categories yet.

---

## The header is the index

Wherever a class's implementation lives across multiple TUs, the
header becomes the table of contents. Without explicit pointers, the
split fragments findability instead of improving it.

Two pieces, both required:

1. **Top-of-file commentary block** naming the categories, the file
   convention, and concrete "where do I find X" examples.
2. **Per-method category tags** — a trailing comment on each
   declaration naming its category, spelled out, not coded.

```cpp
void on_save();         // handler: documents
void update_title();    // helper
void setup_menu();      // zone: menu
```

Together these make finding code a deterministic two-step: read the
header, read the tag, open the file. This is non-optional. A split
without it is half-done. `include/Shell.hpp` is the worked example
in this codebase.

---

## Discipline through inheritance

Project-namespaced wrappers around framework primitives let you make
project rules **mandatory at construction**. The framework class
becomes the base; the wrapper enforces the invariant in its only
constructor.

The motivating case: GTK widget names are enormously useful (the
Inspector, CSS targeting, a backtrace frame that names itself) and
`set_name()` is optional and easy to skip. Over hundreds of sessions
the Inspector becomes a forest of anonymous nodes. `jot::widgets::Named<W>`
takes the name in its only constructor. Naming becomes a compile-time
invariant and an unnamed widget isn't expressible in app code.

Benefits beyond the original invariant: the widget vocabulary becomes
a curated subset instead of "anything in gtkmm"; project conventions
land at the wrapper level and apply uniformly; cross-cutting changes
become one wrapper edit.

**For a new project: write the wrappers on day one.** Don't wait
until the codebase feels the pain — by then the migration is a
multi-session refactor.

---

## The substrate registry is a live address book

The registry mirrors what exists **right now**, not what the source
could build. Conditionally-built surfaces only appear while alive.
Address what's there, not what could be there.

**Self-rebuild needs explicit pre-clear unregistration.** When a
handler rebuilds its own widget tree, the framework's destroy-idle
and the rebuild-idle run at the same priority with no ordering
guarantee: new widgets construct and register before the old ones
finish destructing, and the registry sees a duplicate name. The
structural answer is an explicit `clear()` that walks the subtree
being torn down and force-unregisters, then removes from the parent.
Idempotency lets the destructor's eventual unregister no-op safely.

The bait in this trap is that *timing patches feel diagnostic*.
Bumping an idle to a 1ms timeout widens the window and may make the
crash go away on your machine. The bug is still there. **A timing
patch that survives the structural fix is worse than no patch at
all**, because the next reader thinks the timing is load-bearing.

### The `unregistered_t` primitive

A widget can opt out of the registry via a parallel tagged
constructor: it keeps the type, the name (for the Inspector), and the
lifecycle; only the registry entry is skipped. Use it wherever a
widget is constructed in a shape that would force two live instances
to claim the same name — per-row content in a list, per-click
popovers built in a lambda, per-loop helper multipliers, per-rebuild
factory lambdas.

The test: imagine addressing the widget by name from a debugger. If
a per-row name like `row_visible_23` is useful, the widget wants
registration. If you'd instead address the *model object* the row is
drawn for, the widget wants the tag.

---

## Per-area logging with runtime toggle

Logging is a design surface, not a sprinkle of `LOG_INFO`. Every
method belongs to a *log area* named by concern, not by file. Each
area has a runtime-toggleable level. When something goes wrong: turn
the suspect area to TRACE, reproduce, read the trace.

Three benefits: granular signal-to-noise; a new reader can list the
areas and see the conceptual decomposition; and it pressures design —
a method that can't decide which area it belongs to is doing too
much.

**Don't strip diagnostics on milestone schedule.** Strip when the
*next* arc doesn't need them. Keeping a debug consumer alive one more
milestone costs near-zero; re-adding it for an unexpected diagnosis
costs a session.

---

## Pumps at conceptual seams

Find the seam where a concept lives, build a small abstraction at
that seam, compose callers. Structural fix; the problem class goes
away. A procedural fix patches every instance.

**Test for "is this a pump?"** — if the operation has a natural
inverse (encode/decode, parse/format, normalize/denormalize), the two
halves belong **in the same file** so changes force seeing both
sides. Bidirectional pumps prevent skew between halves.

When you write the same 5-10 lines in three places, that's a pump
waiting. Extract before the fourth — extracting after-the-fact
requires migrating prior sites, and that migration risks behavior
drift.

### A pump must be the only path

A helper that routes a class of operation correctly is only half the
structural job. If the thing it wraps is still reachable from the
same call sites, new callers (or old ones copied for a new shape)
skip the helper and the bug class returns through the back door.

Make it enforceable: private the wrapped field so callers physically
can't reach it, or move the helper into the same TU and make the
access static-internal, or at minimum a grep-level CI rule that fails
the build when the underlying form appears outside the helper.
**The pump existing is half the job; the pump being unbypassable is
the other half.**

---

## Round-trip fidelity is non-negotiable

User-typed text survives load-save unchanged. Period.

Sanitization applies to derived ids only — never to user-visible
content. When designing a format or a persistence layer, ask: "if the
user types X, does X come back exactly?" If you can't promise yes,
the format has a bug. Even invisible transformations (whitespace
normalization, encoding conversion, case folding) are losses that
surface eventually.

Prefer plain text formats specifically because round-trip fidelity is
verifiable by reading the file. **Binary formats hide skew.**

---

## Tracking is an illusion; the model is the truth

User input — mouse position, modifier state, key sequences — is a
*method of producing model mutations*. It is not the operation being
performed.

A "drag this node onto that one" gesture is one field write:
`node.parent_id = target.id`. A drag handler that instead copies the
node, appends the copy under the target, and deletes the original has
mistaken the gesture for the operation — and it will fail the moment
the node has children, or the moment anything else in the system
holds the node's identity.

Where this rule wouldn't hold: applications where the path of motion
is itself the data (paint brushes, signature capture, gesture
recognition). jot has none of those.

---

## "Guest in the framework's home"

When the framework behaves unexpectedly, default assumption: we're
using a widget non-idiomatically. Look for the idiomatic alternative
before fighting with workarounds. GTK has its own opinions about how
things compose; the project lives inside that opinion. The path of
least resistance is usually correct.

Same principle for any framework — sigc++, nlohmann/json, Cairo,
Pango. The framework's design choices encode constraints you don't
see at first glance.

### "What's different about this widget compared to the ones that work?"

When one widget misbehaves where its siblings behave, the productive
question is comparative, not API-level. Line up the broken thing next
to the working ones and look at the diffs — base class, ownership,
who constructs it, what the show path does. The diffs that turn out
to matter often look like trivial structural choices until you
measure. "Non-idiomatic" includes *composition, ownership and
lifetime patterns*, not just direct API calls.

---

## Concatenated paths are a silent-failure surface

When a framework joins two pieces of configuration into a runtime
lookup key — a prefix plus a filename, a namespace plus a class, a
base URL plus a route — the join is silent. If either piece is wrong,
lookups return null with no error. The build is green; the path
doesn't resolve.

The canonical case is `glib-compile-resources`, which concatenates
the `<gresource prefix=…>` with the literal `<file>` content. A file
declared as `<file>icons/foo.svg</file>` under prefix `/app/icons`
lands at `/app/icons/icons/foo.svg`. The fix is
`<file alias="foo.svg">icons/foo.svg</file>`.

**Whenever the framework joins configuration at runtime, the join
itself is a verification point.** Find the tool that prints what got
joined (`gresource list <binary>`) and run it after every change to
either half. Don't trust that the compile succeeded.

Same family: path joins by string concatenation rather than
`std::filesystem::path operator/`; HTTP route prefixes where one
layer strips a slash and another adds one.

---

## "In your face" design

Controls are visible, not buried in modal Preferences dialogs. Nested
scope lives in the inspector in scope order. The user can see the
model, point at the thing they want to change, and change it.

The "could a user point at it" test extends beyond UI. When designing
a data structure, a menu, an API surface — ask: "could a user or
another developer point at this concept and name it?" If yes, it
deserves first-class structural representation. If no, you may be
inventing concepts the system doesn't actually have.

---

## Don't predict, look

When uncertain about what code does, read it. Don't reason from
priors.

In long-running projects your model of the code drifts from reality.
Past-you wrote it; current-you remembers what past-you *intended*;
the actual code may diverge from both. This applies especially to AI
assistance — the assistant carries an accumulated model from prior
sessions that can be wrong in non-obvious ways. **When the assistant
states what code does without having just read it, that's a
hypothesis pending verification, not a fact.**

Two-attempts-then-instrument is the floor for diagnosing bugs. Logs
are cheap; theory-patches are expensive.

---

## The instrument is part of the claim

> Before believing "X is broken," run the case that must work.

"Don't predict, look" says stop reasoning from priors and read. This
is that rule's failure mode: you looked, and the thing you looked
*through* lied to you.

Plunk s002: a sandbox reproduction reported that a GResource icon
path didn't resolve. Four variations agreed. The obvious conclusion
was that the resource arrangement was wrong. It wasn't — the sandbox
had no gdk-pixbuf SVG loader, so *every* custom SVG failed, including
definitionally correct ones. A rewrite of working code was one step
away. The tell had been sitting in the output for rounds: system
icons resolved and every custom one failed.

The asymmetry to internalize:

- A **positive** result is largely self-validating. A broken
  instrument rarely produces a correct-looking success.
- A **negative** result is worth exactly as much as the detector
  behind it. Absence of evidence requires a working detector.

So the discipline is asymmetric too: believe your successes cheaply,
and make your failures earn it. **Run a control that should PASS.**
It costs one extra line.

### A precedent's preconditions travel with it

Same session: every symbolic icon in Adwaita declares
`width="16px" height="16px"`, 200 out of 200. That was taken as
licence to declare a width on an icon with a 1000-unit viewBox. But
Adwaita's viewBox is `0 0 16 16` — the numbers *agree*, so the
declared size costs nothing. On a 1000-unit viewBox the identical
attribute pins the drawing to a 16-pixel speck.

"200 of 200 do it" was true and useless. When lifting a pattern from
another codebase, find the invariant that makes it correct *there*
and check it holds *here*. The precondition is usually unstated,
because in its home context it's too obviously true to write down.

### A fix that works is not a fix you understand

**Understanding the mechanism is what tells you where else the bug
is.** In the icon case the same pinning was sitting unhit in the
About window, where a 48px-declared icon was about to be rendered
into an 88px hero. The empirical fix closed one instance; the
mechanism closed the second before it was ever seen.

Not every fix earns that effort. But any fix whose mechanism you
can't state is a fix you cannot generalise, and you will meet its
siblings one at a time.

---

## Check the elevation before fixing

When a fix feels load-bearing for a constraint that's everywhere,
check whether the constraint is real or inherited.

Most "obvious" architectural shapes aren't design decisions — they're
muscle memory dressed as design rules. The tutorial showed it that
way; the first instance copied the tutorial; every subsequent
instance copied the first. Nobody asked whether the shape was right.

**The tell:** a fix has to ride into every instance of a pattern, the
fix is correct at each site, and the pattern is widespread but rarely
questioned. When all three are true, step back: is this pattern
necessary, or inherited? Sometimes the answer is "necessary — changing
the shape costs more than the fix." Sometimes the shape itself is the
bug.

Curvz's worked example: four heap-allocated dialogs needed a
force-unregister dance on every close. The local fix was correct and
generalised. The elevation question was whether they should be
heap-allocated at all. Converting them to hide-on-close singletons
made the entire discipline evaporate.

Adjacent failure mode: treating "this is how we always do it" as
proof the shape is right. Conventions that survived because nobody
re-examined them aren't the same as conventions that survived
re-examination.

---

## A moved responsibility looks like a bug in your code

The app came up light on a dark desktop. Every instinct says find
what's wrong in the window — a stylesheet, a hard-coded colour, a
stale theme lookup. All wrong, because there was nothing to find. The
defect was an **absence**: the app never asked the desktop what it
wanted.

This is a class, not an incident. Across a major version a framework
moved a responsibility onto the application. GTK3 expressed "dark" as
the theme name, so acting on the preference and having a theme were
the same act; GTK4 split them and left the acting to the app
(libadwaita is where that acting usually lives). Nothing regressed.
The code kept doing exactly what it always did, which was nothing,
and *nothing* stopped being sufficient.

The diagnostic signature:

- **The symptom is a wrong default, not a wrong result.** Wrong
  results have a line of code that produced them. Wrong defaults have
  no line at all, so reading the code proves innocence repeatedly
  while the bug stays.
- **Every component individually looks correct.** Auditing them is
  time spent confirming nobody is at fault.
- **It appeared on an upgrade nobody made.** Distro moved, toolkit
  moved, the app is the same bytes.

The question that finds it is not *what is wrong here* but **what
used to be done for me that isn't any more.**

### Corollary: ask the cross-desktop interface, not the popular one

Two sources hold the appearance preference: GNOME's private GSettings
key (what the control panel writes, what every search result reaches
for) and `org.freedesktop.appearance` via the XDG portal (what every
desktop is expected to answer). Taking the first hard-codes another
desktop's schema name into your app and works only where that desktop
is installed.

**When a platform value has both a popular source and a standard one,
the popular source is a private implementation detail you're reading
over the owner's shoulder.** It'll be correct on the machine you
tested and blind everywhere else, silently.

### Corollary: a fallback that aborts is worse than the bug

`g_settings_new()` on an uninstalled schema does not return null. It
**terminates the process.** The obvious two-line fallback turns "this
machine has no GNOME schemas" into a startup crash — strictly worse
than the light window it was written to fix. A fallback path runs, by
definition, on configurations you did not test; it earns *more*
suspicion than the primary path, not less. Look up the schema, check
the key, then open it.

### Corollary: the tri-state that gets flattened

The preference is `0 = no preference, 1 = prefer dark, 2 = prefer
light`. **No-preference is not light.** Written as bare integers,
`!= 1` looks like a simplification and silently converts *prefer
light* into *no preference*; written as a named enum, the same edit
reads as the mistake it is. **Name the states of anything a later
reader could mistake for a boolean.**

---

## Lifetime shape is design, not convention

Long-lived objects (main window, panels, singleton dialogs) build
once and mutate often; substrate widgets inside register once and
live until shutdown. Short-lived objects (transient dialogs,
popovers, context menus) build, present, tear down; their registration
story is structurally hard.

The mistake is treating the choice as stylistic. It determines what
coordination discipline the object needs. Pick wrong and the
coordination work either disappears (you got lucky) or multiplies.

**The tell that lifetime is wrong:** a coordination problem the rest
of the codebase doesn't have. Look at sibling objects of the same
category. Are they coordinating around the same problem, or silent on
it? If silent, your object chose a different lifetime shape than its
siblings.

Don't convert lifetime shapes mid-session unless the conversion is
the headline. Make the flip its own milestone, and the follow-on
sweep a separate one, so each is evaluated on its own.

**Born-modern is the default now.** New dialogs and windows copy the
shape from a recent sibling: owned by the main window, `set_hide_on_close(true)`,
`present()` to show. The conversion arc is a legacy-rescue pattern,
not the standard.

---

## Singletons for many-fingered widgets, lazy and class-local

> A widget that (a) is reused from many fingers through the code and
> (b) carries no per-host state between presentations should be a
> single instance, lazily constructed on first demand, living until
> process exit, accessed via a static method on the class itself.

Class-local placement matters as much as the singleton. A bare global
or a `Utils::singletons` entry becomes orphaned from the type it
serves; readers write a second instance because they don't know the
first exists. `ColorPickerPopover::shared()` sits in the type's own
header, three lines from the class doc.

Lazy construction matters because GTK constructors often need a live
widget tree. A function-static *inside the accessor* runs the first
time something asks, by which point an asker exists.

```cpp
static T& shared() {
    static T* instance = new T();
    return *instance;
}
```

Heap allocation plus leak-at-exit is intentional: GTK destroys
widgets through their parent toplevel during shutdown, before a
function-local-static destructor would run.

**The two tests:** many fingers (held as a member in many classes with
no semantic distinction between copies — boilerplate, not variation),
and no retained state between sessions (everything needed arrives as
arguments to `open()`). Mutually-exclusive-at-presentation is a clean
tell that the second holds.

### Singleton callbacks must not capture by raw `[this]`

A singleton that stores host-supplied callbacks outlives every host.
Pre-singleton patterns were safe by *coupling lifetimes*: the host
owned the widget, the widget owned the callbacks. Singleton-ising
breaks that. A captured `[this]` from a dead host is a delayed crash.

Three workable patterns: a **liveness flag** the host owns
(`shared_ptr<bool>` captured by value, flipped false in the dtor,
checked at the top of the lambda); **capture stable references** only
(an id, an enum slot, a long-lived pointer, and walk the model);
or **host the callbacks in a long-lived parent**.

Why this isn't tracking: the popover can't reliably identify whether
its current session's host is alive — raw pointer identity isn't a
lifetime predicate. The right place to track lifetime is *at the
boundary the lifetime crosses*, which is the host's own callback.

### Re-parenting a managed widget across toplevels needs an explicit ref

`make_managed<T>()` + `set_parent()` means the parent owns the only
reference. For a singleton that moves between toplevels, `unparent()`
→ `set_parent(new_root)` finalizes the widget mid-transition and the
next call trips a `GTK_IS_WIDGET` assertion. One line at
construction fixes it:

```cpp
g_object_ref(G_OBJECT(pop->gobj()));   // we own a strong ref forever
```

Never released. The GObject equivalent of the singleton holding one
share.

---

## Belt-and-braces controllers when a keystroke must survive focus shifts

GTK4 routes keystrokes through whichever widget holds focus. A
window-level CAPTURE controller catches a key *unless* a descendant
claims it first, and "first" depends on focus chain plus internal
widget behavior in unpredictable ways. A TextView's edit machinery
eats text input; a tab strip eats plain space.

The robust pattern is **two controllers, same handler, different
anchors**: one on the window (CAPTURE), one on the inner target
(CAPTURE). Either fires, both call the same code, the keystroke is
claimed so it can't fire twice. The handler must be idempotent
against double-fire.

These are siblings, not duplicates — they cover different failure
modes of GTK's focus routing. Removing one re-opens the bug class it
was added to close.

---

## `Glib::RefPtr<T>` is `std::shared_ptr<T>` in modern gtkmm

Pre-2.62 gtkmm shipped its own `RefPtr` with member casts. Most
reference material on the web predates the change. In modern gtkmm
`Glib::RefPtr<T>` is a `using` alias for `std::shared_ptr<T>`, so
those member functions don't exist:

| Old (gtkmm-era) | New (std-alias) |
|---|---|
| `Glib::RefPtr<U>::cast_dynamic(p)` | `std::dynamic_pointer_cast<U>(p)` |
| `Glib::RefPtr<U>::cast_static(p)`  | `std::static_pointer_cast<U>(p)`  |
| `Glib::RefPtr<U>::cast_const(p)`   | `std::const_pointer_cast<U>(p)`   |

The error (`no member named 'cast_dynamic' in 'std::shared_ptr<T>'`)
is diagnostic but easy to miss, because the call *reads* like gtkmm.

---

## Audit checklists pay multiple dividends

When work is large enough to be uncomfortable to hold in your head,
write the checklist *before* doing the work. The artifact exists once;
the value compounds across the original migration, later verification,
and later diagnosis.

A good audit checklist states its scope explicitly (which sites, which
files, which predicates), classifies each item by required action,
carries line-number references as of audit time, and notes deferrals
with reasons. **The scope statement is as important as the findings** —
the most valuable diagnosis Curvz got from an audit doc was "the audit
was scoped past external callers," which was only an immediate insight
because the scope had been written down.

---

## Dead mapping entries lie about structure

A mapping table is a document about the structure of the code, not
just a lookup. Every entry tells a future reader: this association
exists, and the thing on the right is a real place.

Entries pointing at places that don't exist compile fine — they're
string-to-string. They make the mapping *look* exhaustive and the
codebase look more symmetric than it is, and the next reader spends
an hour looking for a section that was never built.

**Sweep dead entries when fixing adjacent live ones.** Grep each
target; if it has no binding site, remove it. The grep takes seconds.
When *reading* a mapping, spot-check a few — if any are dead, the
rest need an audit.

---

## Routing through indirection means routing through every level

A routing helper that walks a tree to put it in a desired shape must
touch **every level** the desired shape requires, not just the
endpoints. Flipping a leaf flag and a top-level group flag works
while the tree is exactly two layers and fails silently at three —
the leaf is "visible" in its own state but sits inside an unopened
parent.

Corollary for open/close verbs on nested UI: **open is
order-dependent** (outer-first, walk the ancestor chain), **close is
monotonic** (order-independent), and **close does not cascade up**
(that would close siblings the caller didn't ask about).

When the parent resolver doesn't know a key, **return empty — don't
guess a default parent.** A resolver that guesses turns a typo into a
side-effect.

---

## Branch off the latest, not the baseline

When a session has multiple milestones touching the same file, each
subsequent milestone's working copy must start from the **most recent
shipped version**, not from the session-start codebase.

The failure is mechanical: m3's source dir gets cloned from
session-start because that's the obvious source of truth, but m1
already edited the file. Applying m3 overwrites m1's edits. Symptom:
a method that was working produces a linker error — its *definition*
is gone, its declaration and call site survive.

Keep a `latest/` pointer at the most recent shipped tree. Copy from
it for any file an earlier milestone in this session already touched.
Session-start is fine only for genuinely new files.

---

## Verify before assuming fit

When new work needs to reuse an existing class, helper, or command,
the **first task of the milestone is to read that thing's body** —
before designing a replacement.

The discipline is symmetric. Sometimes the existing shape doesn't fit
and a parallel class is right (Curvz's `ZOrderCommand` keyed on an SVG
id that's empty for script-minted objects — multiple siblings would
collide on an empty key). Sometimes it fits exactly and the new work
is a thin shell over it.

The discipline isn't "always reuse" or "always parallel." It's
**always verify first.** The first-task read is the falsifiable step.

**The tail:** once a broad-capture class fits the first caller at a
seam, sibling callers tend to fit unmodified. Curvz ran seven
consecutive "fits unmodified" on one command across five sessions.
The structural property to notice: a command that captures a *whole
struct* rather than individual fields covers an arbitrary set of
single-field edits at zero marginal design cost. **Commands designed
for breadth pay back over the long tail of verbs they enable.**

The tail breaks where the capture boundary breaks. Recognising the
break *is* the verify-before-assume read; you can't skip the read
because the tail has been long.

---

## Close the architectural gap to unblock the work above it

When a milestone is blocked behind a pre-existing architectural gap
on the layer below, **close the gap first** — as its own milestone —
even though closing it doesn't move the visible scoreboard.

The shape: the intended milestone looks mechanical; the first-task
read discovers the method it would reuse has never been undoable /
containment-checked / gated; shipping over it would close the work on
a technicality while breaking an invariant the rest of the system
holds. The right answer is two milestones. m1 builds the
infrastructure and migrates existing call sites; m2 ships the work
over the now-stable base.

The temptation is real — the wrap is small, the arc is ready to close,
momentum says ship. **When you find a gap on the path, the gap is the
milestone.**

### Bridge milestones

A **bridge milestone** ships complete infrastructure with **no caller
yet**, gaining its caller in the immediately-following milestone. Its
acceptance criteria are **build + clean run + regression-free** — not
"a test passes," not "the user observes new behaviour."

This is not "shipping incomplete work." It's shipping a complete
piece of infrastructure under an explicit no-caller-yet contract, and
that contract *is* the acceptance criterion.

A milestone that ships infrastructure AND its first caller together is
not a bridge — it's a single-milestone arc that happens to include
infrastructure. The bridge shape applies when the split is deliberate.

**The bridge is a narrow exception, not the default order.** It is
for when the layer below genuinely BLOCKS the layer above — the
method you'd reuse isn't undoable, the path isn't containment-checked,
the verb isn't gated. It is NOT for when building underneath first is
simply the tidier way to work. Reaching for it on tidiness grounds
produces a run of milestones that each end underground, which is the
failure the next two entries are about. If you can't name what the
consumer is blocked ON, you don't have a bridge; you have a
preference for building bottom-up, and it should terminate at a
surface like any other milestone.

---

## A milestone ends at a surface

> **Banked — one instance, and that instance unverified.** Proposed
> from a working-practice conversation rather than earned by a project
> that hit the pain and recovered. The candidate instance is Optik /
> Quire / delr, all three pipeline-shaped tools whose disappointments
> are suspected to be UI-shaped; none has been read against the claim.
> Graduates when a project runs this and it holds, or when that
> postmortem confirms the diagnosis. Until then, treat as a strong
> working hypothesis, not settled canon.

Core work may lead inside a milestone. A milestone must not *end*
inside the core.

The argument isn't morale, though morale is real. It's that
**convergent evidence has two channels and the assistant can only
reach one of them.** A green build and a passing headless test say the
code does what it was told to do. They say nothing about whether it
was the right thing, and "is this the right thing" is precisely the
judgment that belongs to the person who can look at it. So a milestone
that lands entirely underground has been validated by the weaker
channel alone — and by the project's own standard for *done*, it
cannot be called done at all.

That is the strong form of the rule: not "invisible milestones are
unsatisfying" but **"invisible milestones are unverifiable."**

The second argument is that **the design questions live in the
surface, and they are invisible from underneath.** A packet sniffer is
the clean example: sockets, BPF filters, header parsing and ring
buffers could absorb six sessions with nothing to look at but a rising
test count. Meanwhile the packet list is where you must decide what a
packet *is* to this app — which columns, so which fields are
first-class; whether a row is one frame or one reassembled message;
what happens when fifty thousand arrive in a second; whether the
detail pane is a tree of decoded layers, which means the parser has to
produce a tree and not a flat struct. Every one of those is a model
decision. Build the engine first and they get answered by accident, in
whatever shape the sockets code found convenient.

**The surface is not the reward for the real work. It is the
interrogation that tells the real work what shape to be.**

What this does NOT say:

- Not "UI first, model second." The order inside a milestone is free.
- Not "split the work evenly between UI and core." Even-division is
  unenforceable — evenly by lines, by hours, by sessions? — and wrong
  in the cases where it's wrong. A parser that genuinely needs four
  days shouldn't spend half its budget padding a surface to hit a
  ratio. A rule that gets ignored the first time it's inconvenient was
  the wrong rule.
- Not "the surface has to be good." Crude is fine. A tree with three
  hardcoded rows and a pane that shows only a title is worth more as a
  checkpoint than a perfect model with a round-trip test, because the
  first one can be looked at and argued with and the second can only
  be taken on faith.

The test at milestone close is one question: **did anything change
that a person can look at?** Yes or no, checkable, and a "no" means
the milestone isn't finished — the same shape as the four-case table
in RULES.md, applied to the milestone rather than the bug.

Companion to **Visual gaps surface in field use** (below), which says
the eyes catch what the trace can't. This entry says the corollary:
**give the eyes something to catch.**

---

## A once-per-process assumption expires the day something forwards

An override that is only ever reached once can quietly encode "once"
in its body -- `m_thing = new Thing()` with no guard -- and be correct
for as long as that stays true. The day a second entry point appears,
the assumption does not fail loudly. It succeeds twice.

jot's instance (s008): `App::on_activate()` unconditionally built the
main window. Adding `jot --capture` meant a forwarded command line
called `activate()` on the already-running instance, which built a
**second window with a second store over the same files** -- two
writers to one project file, and no error anywhere.

What caught it was the widget registry warning about duplicate names,
which is the address book doing its job: a second copy of a singleton
surface announces itself by colliding.

**So: when adding a second way to reach an existing entry point, read
that entry point for what it assumed about being reached once.** The
compiler cannot ask the question, and the failure mode is duplication
rather than a crash.

---

## A registered option is removed from the argv you receive

Registering a command-line option with a framework so it appears in
`--help` is also registering it for PARSING, and the parser consumes
it. Code downstream that reads the raw argv then finds the flag
missing and concludes it was never passed.

jot hit this exactly (s008): `--capture` was registered as a BOOL so
`--help` would list it, and the argv reaching `on_command_line` had it
stripped -- so every `jot --capture` parsed as no-capture and silently
became a plain launch. The flag has to be read from the options the
framework produced, not from the argv it handed back.

The general shape is worth more than the instance: **when a framework
offers to handle something for you, find out what it CONSUMES, not just
what it produces.** Half-using a parser -- registering for the help
text, parsing by hand -- is the arrangement where the two halves
disagree silently.

---

## An absent answer is the hardest wrong answer to see

A stale index entry at least renders: a backlink to a note that no
longer points here is clickable, goes somewhere, and can be caught by
someone following it. **A row that was wrongly filtered out leaves
nothing behind at all.** Nothing draws, nothing logs, and the absence
looks exactly like having nothing to show — so the surface cannot
report the fault even in principle.

jot's instance is availability (s007): whether a todo can be done now
is derived from parent status, sibling order, done flags and defer
dates. Get it wrong and a task silently stops appearing in Today, and
**a task wrongly hidden is a task you do not do.**

The rule that follows: **when a wrong answer would be invisible rather
than merely wrong, the answer belongs in a pure function under the
test harness**, not in the view that would have drawn it. Not because
views are careless, but because a view has no way to notice.

Corollary, and it is what keeps the discipline honest: **cache
membership, never verdicts.** `core::TaskIndex` holds the set of nodes
that are todos, in document order, and re-derives every verdict on
every query. Membership can be checked against the model in one
comparison — rebuild and compare — which is a test you can actually
write. A cached verdict has nothing to compare against.

---

## A pane with a sibling invalidates every verb that named it

When a container gains a second child, every existing line that acted
on the old child has to be re-read, because "the tree" quietly stopped
meaning "the left pane".

s007 put Notes and Today in a stack inside jot's left pane. The F9
focus-mode toggle still said `m_tree->set_visible(...)` — which would
have hidden the tree and left the tab bar floating over an empty
stack. One line, compiling perfectly, silently dismantling the feature
s006 was built around.

The compiler cannot help here: both the old child and the new parent
are valid widgets and both expressions type-check. **Grep for the old
child's name after any structural change and read every hit**, rather
than trusting that the places which needed updating are the places you
were already editing.

---

## Fixture data through the real seam

> **Banked alongside the entry above**, and the mechanism that makes it
> safe. Same provenance, same unverified status.

The objection to ending every milestone at a surface is a good one:
build the UI first and you get a model shaped by whatever the widget
made convenient. That failure is real and we have the specimen.

Notr — a tree-of-notes app — stored a flat node set keyed by parent
UUID, which is the right shape and makes a move one field write. Its
drag handler ignored all of that and worked through the tree widget's
API instead: it copied the node, appended the copy under the target,
and deleted the original. Consequences: it could not move a subtree at
all (it refused, and told the user to move the children first), and it
minted a new identity on every drag, which would have broken every
inbound link the moment links existed. The model was correct and the
UI quietly stopped using it.

So the discipline is not "build the model first, in private." It is:

> **The first surface is fed by fixture data, and the fixtures arrive
> through the same interface the real implementation will use.**

The packet list reads from a `PacketSource` that returns eight
hardcoded frames. Nothing captures anything. Later, a live capture
implements `PacketSource` and the list doesn't change by one line —
which is the proof that the UI never got to dictate the model's shape.
If instead the list reads a `std::vector` directly, you have built a
UI-shaped model and you will pay for it at the swap.

The swap is the test, and it's falsifiable: **if wiring the real
implementation requires changing the surface, the seam was drawn in
the wrong place.** That's a finding, not a chore — it means the fixture
milestone did its job and found the mistake while it was still cheap.

Why this composes with the rest of the canon:

- It IS "pumps at conceptual seams," applied at the start rather than
  discovered later. The interface between surface and source is a
  seam; fixtures are just the first thing plugged into it.
- It IS "tracking is an illusion; the model is the truth" — the
  surface is a renderer over the model, and the fixture proves it by
  making the model's only contribution an interface.
- It gives "don't predict, look" something to look at in the first
  milestone rather than the tenth.

The shape of a founding milestone, then: a window with the real
surfaces, fed by fixtures through the real seam, ugly and hardcoded
and *lookable*. Then the model thickens underneath something already
being watched.

---

## Prediction before observation

When shipping a milestone, state the expected result **before** the
run — test counts, acceptance criteria, what should visibly change.
The retroactive version ("yep, that's what I expected") carries no
information; anything can be rationalised after the fact. The
pre-stated version can be *wrong*, which is the whole point.

Curvz ran fourteen consecutive matching predictions. The streak is
informative on its own terms: it means the work's shape is known
before it ships, not discovered. A missed prediction would be *more*
informative than a match.

The shape: "expected: N pass / M fail / K error" alongside the
download link, with a one-line gloss of what the K errors are if
non-zero. For work without a test count, pre-state the acceptance
criteria instead.

---

## Visual gaps surface in field use, not in smokes

A clean trace is necessary and not sufficient. A test exercises what
it can MEASURE — dispatch, round-trip, stack effects, counts. It
cannot exercise what only an eye can SEE: a colour painted on a
zero-width stroke that renders nothing; a row added to a panel and
never removed; geometry that lands off-page because an axis flipped.

In each of those Curvz cases the assert passed truthfully at the data
level while the canvas was wrong at the pixel level. **A milestone
with a clean test and no visual confirmation is half-shipped.**
Every appearance-bearing milestone earns a visual-confirmation step.

---

## One-shot ships come from surfacing forks before zipping

When implementation forks are surfaced and resolved **before** the
zip — in the pre-implementation message, not mid-implementation — the
implementation reliably rides clean on first build.

The shape: the pre-implementation message names the architectural
calls being made without asking, surfaces the genuine forks, Scott
ratifies or redirects, *then* code writes.

What this does **not** claim: that one-shot ships are always
achievable. It claims that when they happen, it's reliably because
the forks were surfaced before zipping — not because the work was
simple or lucky.

Related: handoffs that name forks **with defaults** enable in-session
execution without polling. Handoffs that name forks without defaults
force a poll-or-guess.

---

## Three-doc top-level structure

Three documents at the project root cover three different concerns,
plus the per-session handoff:

- **`ARCHITECTURE.md`** — what the project *is*. Stable. Updated when
  major systems land or change shape. Reader: someone orienting.
- **`ARC.md`** — what the project is *doing*. Active. Updated as arcs
  progress. Reader: someone joining a session in progress.
- **`CANON.md`** — what we believe *carries forward*. Cross-project.
  Reader: someone starting a new project.
- **`HANDOFF.md`** — session-to-session continuity. See RULES.md.

Resist one giant README that mixes them. The scopes are genuinely
different and mixing dilutes each.

---

## A C header without G_BEGIN_DECLS is a link error, not a compile error

s009 called `e_cal_system_timezone_get_location()` from C++. It compiled
cleanly and failed at link: EDS's `e-cal-system-timezone.h` has no
`G_BEGIN_DECLS`, so the declaration picked up C++ linkage and the symbol
never resolved.

That is the expensive order to find out — a compile error names the line,
a link error names a mangled symbol and leaves you guessing which header
lied. A glance at a C header before calling it from C++ costs nothing.

The corollary was the better half: **working around it was worse than
dropping it.** The function existed to turn an epoch into a zone-aware
time, and the two cases wanted different things anyway — UTC for a timed
value, and three numbers out of `localtime_r` for an all-day one. The
workaround would have preserved a call that was never the right shape.

## An instrument that models half a pipeline reports confidently on the wrong half

s009's probe asked EDS the same live query `gnome-shell-calendar-server`
asks, and disagreed with reality twice. Both times the instrument was
wrong, not the code:

- the calendar server RE-FILTERS everything the query returns, by
  DTSTART, in its own process — so EDS over-returning is harmless and
  testing only EDS's filter tests the wrong stage;
- the server calls `e_cal_client_set_default_timezone()` before
  querying, and a DATE value carries no zone, so without that call every
  date resolved against UTC.

Both of its bugs PRESENTED AS BUGS IN THE SUBJECT. A failing check said
"a todo due tomorrow leaks into today", which is exactly what a real
timezone bug in the writer would say.

So: when an instrument and reality disagree, suspect the instrument's
COMPLETENESS before the subject's correctness — and build an instrument
by copying the real consumer's stages, in order, rather than by writing
an equivalent one. An equivalent is a second dialect, and the two
dialects disagree in exactly the places nobody checked.

This nests under "the instrument is part of the claim": the instrument
has to be reviewed as code, because it makes claims.

## "Wait for connected" is a timeout, not a budget

`e_cal_client_connect_sync`'s `wait_for_connected_seconds` returns as
soon as the backend is ready and gives up after the time is spent. In a
cold session s009 paid the FULL thirty seconds, synchronously, on the
main loop.

The instinct — pass 0, don't wait — was worse: the client came back
immediately and unusable, and the first read then blocked with no
timeout at all. **Read what a timeout argument does when it EXPIRES, not
only what it does when it is satisfied**, and check what the "don't
wait" value actually turns off before reaching for it.

What fixed it was neither value but a MOVE: the slow first connect came
out of the Shell's constructor and went through the existing debounce,
so the window is up before anything crosses the process boundary. A cost
you cannot remove can often be relocated to somewhere nobody is
watching.

## A fingerprint of what you last sent is not a cache of the answer

s009 skips a D-Bus round trip when the projection's FNV-1a fingerprint
matches the last one written. That is safe, and a cached projection
would not be, and the difference is worth stating precisely:

the projection is RECOMPUTED FROM THE MODEL on every call. Only the
WRITING is skipped. So the worst a stale or wrong fingerprint can do is
skip a write that would have produced the same bytes — where a cached
answer could serve a stale projection that nothing would reveal as
stale.

The same shape appears in `TaskIndex` holding membership and never
verdicts. Cache the question, never the answer.

## A feature whose effect lands in another process needs a status line

jot's desktop projection happens in a GNOME drop-down. From inside jot
there is nothing to see — which means that without a status line, both
success and failure are invisible from where the user is standing, and
the only way to discover a broken one is to notice something missing
from a panel you were not watching.

So the surface is not the menu item; the surface is the SENTENCE that
says what landed and when. This is "in your face" design applied to a
result rather than to an error: an effect you cannot observe from the
app needs the app to report it.

The corollary is what turning it OFF must do. Stopping the updates is
not enough — a desktop still showing yesterday's rows is worse than one
showing none, because a stale answer and a correct one look identical.
Off clears.

## Compile both halves of an #ifdef before shipping

s009 shipped a `Desktop.cpp` whose no-libecal fallback branch had never
been compiled by anyone. The sandbox HAD libecal, so it only ever built
the other half; the first machine without it got forty compile errors on
the first try.

The bug itself was a careless `str.replace` with no count, pasting a
block at both of two matching sites. That is incidental. What is not
incidental is that **nothing in the loop would have caught any bug in
that branch** -- not the build, not the selftest, not the probe. An
optional dependency creates a second program, and only one of the two
was ever being built.

So an `#ifdef` that selects between implementations needs a way to force
each side, and both sides get built before a ship. jot has
`cmake -B build-noecal -DJOT_DESKTOP=OFF` for exactly that, and it is a
BUILD switch rather than a user setting -- its job is to make the
unbuilt half buildable on demand.

The general form: **a branch nothing compiles is a branch that is broken
and does not know it.** The same reasoning already governs "a claim in
the doc isn't a fact until a consumer exercises it"; this is the
compiler's version of it.

## Never destroy a widget from inside an event being delivered to it

jot's tree is Pole B: a rebuild destroys every row widget and makes new
ones. `on_model_changed` did that synchronously, and it was correct for
as long as model changes arrived from the tree itself or from a menu.

s006 added a drawer whose date entries commit on FOCUS-LEAVE, and that
broke the assumption without touching the tree at all:

1. focus is in the drawer's due field; the user clicks a tree row
2. the click moves focus -> the entry commits -> a model write
3. the model write rebuilds the tree, destroying the row being clicked
4. GTK's click gesture, still mid-flight, grabs focus on the destroyed
   row and walks off the top of the widget tree

The fix is an idle: the first moment the gesture has certainly finished.
Coalescing comes free with it.

The lesson is not "use idles". It is that **a synchronous
destroy-and-rebuild encodes an assumption about who can author a change
and when** -- and that assumption expires the day a second surface can
write to the model during an event aimed at the first. Same shape as "a
once-per-process assumption expires the day something forwards".

### Corollary: a widget that knows its own name pays for itself in gdb

The assertion was identified from a raw backtrace frame:

```
p gtk_widget_get_name((GtkWidget*)new_focus)   ->  "tree.listrow.7ebcc600-..."
p gtk_widget_get_parent((GtkWidget*)new_focus) ->  0
```

Two commands, no source reading, and the second one named the bug
outright. `Named<W>` is justified in its own header as debuggability
rather than scripting; this is the first time that claim was cashed in
against a real crash, in someone else's GTK version, on an architecture
the code had never run on.

## A stored value's getter must be the setter's own inverse

jot remembered its window size with `set_default_size()` on the way in
and `get_width()`/`get_height()` on the way out. Both are "the size of
the window". They are not the same number: the getter returns the
CONTENT area, the setter takes a size that includes decorations on some
backends.

The result was a window that shrank ten pixels each way **every single
launch** -- 940x620, 930x610, 920x600 -- compounding forever, and
invisible in any one run. Four launch-close cycles in a row is what made
it obvious; one would have looked fine.

`get_default_size()` is `set_default_size()`'s pair, and the round trip
is then exact by construction.

The general rule: **when a value is stored and restored, the getter must
be the SETTER'S inverse, not merely a getter that returns something with
the same name.** Round-trip fidelity is already non-negotiable in this
document for file formats; this is the same requirement pointed at an
API, where the two halves are easier to mismatch because both names read
correctly.

And the test that catches it is not one round trip but SEVERAL. A
one-cycle check passes on a drift of ten pixels; the error only becomes
legible when it accumulates.

## "Consistent with" is not "verified"

s009 was built on the claim that the GNOME calendar drop-down reads
Evolution task lists. The evidence was one line in the calendar server:
a branch that expands recurrences only when the source type is EVENTS,
which would be pointless if task clients never arrived.

That is consistent with the claim. It is not the claim. The events
loader calls `calendar_sources_get_appointment_clients()`;
`get_task_clients()` is a separate entry point feeding the reminder
watcher. A task list is never opened for the events card.

I wrote "verified before it was built" in the handoff. It had not been
verified. Everything after that was done carefully -- a pure/impure
split, a selftest, an instrument that found two real bugs and was itself
debugged twice -- and every bit of that care was aimed at the wrong
question.

**A good instrument aimed at an unverified premise produces confident,
worthless evidence.** The rigour downstream does not test the premise;
it disguises the fact that the premise was never tested.

The tell, in hindsight, is the SHAPE of the evidence. An inference from
an implementation detail ("this branch would be pointless otherwise")
answers "is this possible?" A premise needs "does this happen?", and
that wants the call site or a run. In this case a single command
answered it outright:

```
CALENDAR_SERVER_DEBUG=1 /usr/libexec/gnome-shell-calendar-server --replace
```

It prints a line per client it opens. jot's source never appeared. That
command was available the whole time and would have cost one minute
before the milestone rather than a milestone after it.

### Corollary: the decision/format split is what made the correction cheap

`core::Projection` decides WHAT reaches the desktop and holds no opinion
about the wire format; `Desktop.cpp` writes. When the premise collapsed,
the rule (a todo, not done, with an effective due date), the
availability wording, the WHY line, the fingerprint, the debounce and
every selftest check survived untouched. VTODO-into-a-task-list becomes
VEVENT-into-a-calendar in one file.

That split was made for testability. It paid off as BLAST RADIUS. Worth
remembering when a seam looks like ceremony: the question is not only
"what can I test?" but "what survives if I am wrong about the outside
world?"

## What this document is not

- Not a coding-style guide. C++ formatting and naming conventions
  live elsewhere.
- Not architecture-specific. jot's own systems are documented in
  `ARCHITECTURE.md`.
- Not a tutorial. Each section assumes the reader has hit the pain
  that motivated the rule, or trusts it by analogy.
- Not gospel. Each rule earned its place; no rule is above revision
  when a new pattern argues against it.

Add when a new belief is earned. Revise when one turns out to be
wrong or incomplete. Canon is durable but not frozen.

## Read the predicate, not the first clause of it (s010)

s009 built a milestone on the belief that the GNOME calendar card would
open an EDS task list. It does not -- it opens appointment clients. The
belief came from ONE SUGGESTIVE LINE that was consistent with task
clients arriving and was not evidence that they do.

s010 went to the source and found the filter:

```c
e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR) &&
e_source_selectable_get_selected (e_source_get_extension (...))
```

and nearly missed the SECOND clause, which would have produced the
identical symptom one clause further along: a source that exists,
contains exactly the right data, and is never opened. **When a gate is a
conjunction, reading half of it is the same mistake as not reading it.**

## An async callback is a raw `this` with a delay on it (s010)

A chain that can outlive its object needs a weak handle and a destructor
that nulls it BEFORE cancelling anything. Note the shape of the risk:
the window is exactly as long as the operation the async was introduced
to survive, so the safer the code looks the longer the exposure.

## Pending is a third state and it belongs in the type (s010)

An operation that is neither done nor failed, reported as `ok == false`
with a friendly message, is a caller that will record an error -- or a
fingerprint, or both. If a result can arrive later, the type says so.

## A removal is not visible to the next line that asks (s010)

Registry and IPC state propagates over the bus. Reusing an identifier
you have just freed is a race whose LOSING BRANCH READS AS A TYPE
CONFUSION -- here, connecting to a task list as though it were a
calendar. Pick a new name; the display name is the part users see.

## A connect nothing exercises is a connect that does not work (s010)

The `#else`-branch lesson, applied to a code path instead of a compile
branch. s010's async chain got five probe checks of its own for the same
reason both halves of the `#ifdef` get built: a path that only runs in
production is a path that is tested in production.

## A count assertion is a test that passes for the wrong reason (s011)

Three of s011's first four selftest failures were the TEST's arithmetic,
not the code's behaviour -- `size() == 3` over a fixture whose totals had
been worked out in someone's head. Assert IDENTITY ("is this node in the
result") rather than cardinality: a count is right by coincidence the
first time the fixture grows a node, and wrong by coincidence the first
time it shrinks.

## An app that is not installed is silent, not broken (s011)

GNOME routes notifications by application id and DROPS what it cannot
look up -- no error, no log line, nothing. Any feature delivered by
another process through a lookup needs the app to perform THE SAME
LOOKUP and say so on a surface the user can see. The daemon's own call,
not a guess at it.

Two corollaries found the same day:

- **A resource compiled into the binary is invisible to whoever renders
  it.** An icon another process draws must be a file on disk; a
  GResource bundle ends at its own process boundary.
- **A capability half-declared is a capability removed.**
  `DBusActivatable=true` with no service file is worse than neither,
  because it stops the `Exec` fallback.

## An action a notification triggers belongs to the APPLICATION (s011)

The notification outlives the window that sent it: it sits in the tray
and may be clicked at a process with no window at all. A `win.` action
has nothing to land in, and a failed activation is a return value nobody
reads -- the same shape as s009's unprefixed `toggle-desktop`.

## A grid may not lie by omission; a nudge may be selective (s011)

Two surfaces over one model can hold OPPOSITE filtering rules and both
be right, when they answer different questions. jot's calendar
projection shows blocked and deferred todos because a day grid that
dropped rows would lie about the day. Its notifications hide them
because an interruption about something you cannot act on is what
teaches a person to turn notifications off -- at which point the ones
that mattered are gone too.

## An app the desktop cannot ASK to stop, it KILLS (s012)

GNOME's Background Apps list quits an app by activating its `quit`
action over D-Bus -- `org.freedesktop.Application`, which GApplication
exports for free. An application that never registered that action does
not get asked politely. **It gets SIGKILLed**, and SIGKILL cannot be
caught, so there is no prompt, no flush, and no last write.

The general shape: **when another process is given a button that acts on
yours, find out what that button actually calls before you ship the
thing that makes the button appear.** The polite path and the violent
path look identical from inside the app until the day something is lost.

For jot this meant residency and `app.quit` had to arrive in the same
milestone. Shipping the hold alone would have been shipping a
data-loss feature with a Quit item on it.

## When "close" stops meaning "exit", every prompt hanging off it lies (s012)

jot asked about unsaved notes on window close because close meant exit.
Add a preference that keeps the process alive and that prompt becomes a
lie -- nothing is being lost, the notes are still in memory with the
process holding them -- and **a dialog that cries wolf is one the user
learns to dismiss before the day it matters.**

So the ask MOVES to the path that is now the moment of loss. That path
had no window to put a dialog on, which is the actual work: it presents
one. A quit that stops to ask is startling exactly once.

The audit this implies: when the meaning of a lifecycle event changes,
every side effect hanging off it has to be re-derived, not carried
forward. Flush, geometry and prompt were three lines in one handler and
the right answer differed for each.

## A branch whose wrong answer is SILENT belongs in a truth table (s012)

One outcome was a branch to read at a glance. Three outcomes, where two
of them differ only in whether unsaved work is about to be lost, is a
decision a headless test should be able to drive every row of.

Same seam as `core::Projection`/`Desktop` and `core::Notify`/`Shell`:
the decision is pure, the doing is GTK. It keeps paying -- s010 rewrote
a delivery half wholesale and its decision half did not change a line.

The rows that are the DESIGN, not bookkeeping: residency outranks the
scratch prompt (nothing is lost yet), and a quit in progress outranks
residency (or Quit hides the window and the app refuses to stop from the
one menu that offers to stop it). And the non-row: a quit never consults
the residency preference at all. **An off switch a preference can disarm
is not an off switch.**

## A status line for another process's work must say WHEN (s012)

"On. 1 due todo announced" counts what jot is TRACKING, not what it
SENT, and it carries no time. So a notification fired at LAUNCH for a
four-minute-old deadline reads exactly like one fired at the deadline --
and ten minutes went into rediscovering the difference.

Whenever the effect of a feature happens somewhere you cannot see, the
status line is the only instrument, and a reading with no timestamp on
it cannot answer the question the instrument exists for.

## A hold is a COUNT, and one release too many quits somebody's app (s012)

`g_application_hold()` / `release()` increment and decrement. Two holds
and one release is a process that never exits; one release too many
takes the count under a window that is still open. So the hold gets ONE
writer, mirrored by a bool, and calling it twice is harmless by
construction rather than by everyone remembering.

## A hidden window has no application (s012)

`Gtk::Window::get_application()` returns NULL on a window that has been
`set_visible(false)`. Two consequences, and jot hit both in one
milestone: a notification sent through the window was never sent, and a
re-presented window could not resolve `app.*` through the action muxer,
so its menus and widgets came back insensitive.

**The thing that outlives the window must not be reached THROUGH the
window.** `Gio::Application::get_default()` is the process-wide
singleton and does not care what any surface is doing. This is the same
lesson as an action a notification triggers belonging to the
APPLICATION, one layer down -- and the layer down is the one that bites,
because it looks like plumbing rather than design.

The repair for the other half: `add_window()` again before presenting.
Idempotent, so it costs nothing where the link survived.

## A log line on the wrong side of a guard is a false witness (s012)

```cpp
if (auto app = get_application()) app->send_notification(id, n);
if (auto lg = log::get(...)) lg->info("notified: {}", summary);
```

The second line printed whether or not the first one ran. For four
hours the trace channel reported deliveries that could not have
happened, and every measurement built on it pointed at the wrong
process.

A log line must be INSIDE the branch whose success it claims. If it can
print on the path where the work was skipped, it is not evidence -- it
is worse than no log at all, because it is trusted.

## A verb that cannot fail produces a trace that cannot mean anything (s012)

`g_application_send_notification()` returns void. No ack, no error, no
delivery status. So even called correctly it proves only that a function
was entered.

CANON already says a clean trace means the verb dispatched, not that the
user saw the effect. This is the sharper case: **some verbs do not even
tell you they dispatched.** Before building a status line on top of one,
check whether the API can report anything at all -- and if it cannot,
find the one that can. `org.gtk.Notifications.AddNotification` is the
same wire with a real reply.

The design that follows: **do not record a thing as done until delivery
is acknowledged.** jot marks a deadline announced at send time, so a
send that went nowhere silenced that todo forever. Acknowledge first,
and the existing clock becomes the retry with no retry machinery.

## Do not write into a handoff what has not been observed (s012)

"GNOME lists it under Background Apps with a Quit item" was carried
forward, written as fact, and is false for an unsandboxed app. It was
the one part of the milestone the sandbox could not reach, and it was
stated with the same confidence as the parts that had been compiled and
tested.

A handoff is read by a future session as ground truth. An unverified
claim in it is not optimism, it is a fabricated observation, and it
sends the next session hunting in the wrong process.

Mark what has been SEEN, and mark separately what is expected. The
second list is allowed to exist; it is not allowed to be written as the
first.

## The only copy decides the order of operations (s013)

A pending capture lives in one file and nowhere else until it becomes a
note. That single fact dictates every rule in `core/Pending`, and none
of them is a preference:

- the write is a temp file plus a rename, because a reader must see a
  whole thought or no file, never half of one;
- the read does not delete -- `read_pending` and `remove_pending` are
  separate calls -- because deletion is only safe once a second copy
  exists;
- the flush into the jots folder happens BEFORE the first spool file is
  removed;
- and the drain refuses to run into a scratch buffer, because a buffer
  that Discard can take is not a second copy.

The failure these buy is a DUPLICATE, and that is the trade taken
deliberately: a duplicate is an annoyance the user can see and delete, a
lost capture is invisible and unrecoverable. **When a cheap failure and
an expensive one are the two outcomes, order the operations so the cheap
one is the one that can happen.**

## A parser that holds the only copy must be tolerant (s013)

A file in the pending folder with no front matter could be treated as
malformed and skipped. It is read as a bare thought instead, because
somebody echoing a line into that folder from a shell script has SAID
something, and a spool that ignores what it does not recognise is a
spool that loses notes.

The same reading decides the edge cases: an unterminated header is not a
header (take the file whole), and a capture containing `---` is not cut
at it (it is markdown -- a horizontal rule in a note is not exotic).

Strictness is a virtue in a format two programs exchange. In a format
whose other writer is a human with a text editor, and whose content is
irreplaceable, strictness is a way of throwing work away.

## Activation and presenting are two things (s013)

`on_activate()` presented unconditionally for twelve milestones and was
right every time, because "already running" meant the window was on
screen anyway. Residency broke that assumption without touching the
line: a resident jot has no window, so every arrival -- including a
scripted `jot --capture "milk"` -- pulled the whole app onto the screen.

**What must be TRUE before work can land (the object exists; it is
attached) and what the user ASKED FOR (look at this) were one statement,
and an invariant took the visibility along with it.** Separating them is
two lines and the parameter documents itself.

The general shape: when a precondition and a user-visible effect share a
call, a change in what "running" means will move both, and only one of
them is supposed to move.
