# Third-party components

Three lists, kept apart on purpose: what jot **ships**, what it **links
against**, and what it **learned from**. Only the first carries an
obligation today; the second one starts to when a binary is
distributed rather than source.

## Shipped in this repository

**JSON for Modern C++ (nlohmann/json) 3.11.3** — MIT
`third_party/json.hpp`, full licence in
`third_party/LICENSE-nlohmann-json.txt`.
https://github.com/nlohmann/json

The only third-party code in the tree. Vendored rather than depended on
so that building jot never turns into installing something first; it is
an implementation detail of the persistence pumps and is included
`PRIVATE`, so nothing that links `jot_core` inherits it.

## Linked at build time, not distributed here

Installed from the distribution, dynamically linked. Nothing in this
repository contains their code, so nothing here is governed by their
licences. **That changes when a binary, tarball or flatpak is
distributed:** the MIT notices have to travel with it, and the LGPL
components have to remain relinkable — which dynamic linking already
satisfies, provided the packaging says so.

| Component | Licence | Why jot uses it |
|---|---|---|
| GTK 4 | LGPL-2.1-or-later | The toolkit. |
| gtkmm-4.0, glibmm, sigc++ | LGPL-2.1-or-later | The C++ binding jot is written against. |
| spdlog (vendors fmt) | MIT (fmt: MIT) | Per-area logging, and the diagnostic instrument the rules lean on. |
| libecal-2.0, libedataserver-1.2 (Evolution Data Server) | LGPL-2.1-or-later | The desktop projection — dated todos in the GNOME calendar drop-down. **Optional**: `-DJOT_DESKTOP=OFF` builds jot whole without it. |

## Read, not copied

Acknowledged because the debt is real even though no code moved.

**gnome-shell-calendar-server** (GPL-2.0-or-later) — jot's
`Desktop::occurring()` issues the same Evolution Data Server
S-expression query as its `app_start_view()`, sets the default timezone
first for the same reason, and re-filters results by `DTSTART` the way
its `app_notify_events_added()` does. That is an API call sequence and a
filtering rule learned by reading their source, deliberately kept
identical so jot's instrument and the real consumer could not disagree
about what the calendar shows. No source was copied.

**gnome-shell's `backgroundApps.js` and `dateMenu.js`** — read to answer
two questions that are not documented anywhere else: what GNOME does to
an application with no `quit` action (it SIGKILLs it), and whether a
calendar row can be clicked through to its source (it cannot). Both
answers shaped jot's design; neither produced any code.

## Not third-party

The lifts from Cairn, Notr, Optik, Folio and Curvz named in the source
comments are the author's own earlier projects. The logo
(`resources/icons/scalable/apps/jot-logo-symbolic.svg`), the `.desktop`
entry and the D-Bus service file are original to jot.
