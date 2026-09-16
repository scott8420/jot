#pragma once
#include <spdlog/spdlog.h>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// Log -- per-area logging, runtime-toggleable (CANON: "per-area logging areas
// with runtime toggle"). Every concern gets a named spdlog logger on one shared
// sink; turn the suspect area to TRACE, reproduce, read the trace -- the
// substrate for "two failures = drop to log mode".
//
// Areas are named by CONCERN, not by file. A method that can't decide which
// area it belongs to is probably doing too much -- the discipline pressures
// single-concern functions.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::log {

enum class Area { App, Shell, Registry, Recents, Io, Model, Tree, Editor, Drawer, Desktop };

// The logger for an area (may be null before init()/after shutdown -- callers
// guard with `if (auto lg = log::get(area))`).
std::shared_ptr<spdlog::logger> get(Area area);

// Stand up all area loggers on a shared stdout sink. Call once at startup.
void init();

// Flip one area's level at runtime (the debug-menu affordance in a real app).
void set_level(Area area, spdlog::level::level_enum level);

// ── the consumer that makes the above a fact ────────────────────────────────
// `JOT_DEBUG=drawer,tree ./build/jot` raises those areas to DEBUG; `all` raises
// every one, and `trace:<area>` goes one louder.
//
// This exists because set_level() had NO CONSUMER from s001 until s006 -- a
// per-area logging facility that nothing could actually switch, which is the
// same class of claim CANON refuses elsewhere ("a claim in the doc isn't a fact
// until a consumer exercises it"). It is also how "two failures = drop to log
// mode" gets started without an edit-rebuild cycle first: the instrumentation is
// already in the binary Scott is running, it was just quiet.
//
// Unknown names are named in a warning rather than ignored, because a silently
// ignored JOT_DEBUG is a debugging session that starts by debugging the
// debugger.
void apply_debug_env(const char* value);

}  // namespace jot::log
