#pragma once
#include "core/Projection.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Desktop -- the writing half of the projection, and the ONLY file in jot that
// knows libecal exists.
//
// It decides nothing. `core::project()` produced the rows; this puts them in an
// Evolution Data Server CALENDAR called "jot", where
// gnome-shell-calendar-server finds them and the GNOME calendar dropdown shows
// them on their days. No GNOME Shell extension, no GJS, nothing that breaks on
// the next Shell release.
//
// ── s009 wrote a TASK LIST and that was wrong ──────────────────────────────
// The calendar dropdown's events card opens APPOINTMENT clients only.
// gnome-shell's calendar-sources.c admits every source through one predicate:
//
//     registry_watcher_filter_cb():
//       e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR) &&
//       e_source_selectable_get_selected (...E_SOURCE_EXTENSION_CALENDAR)
//
// A task list fails the first clause and can never reach that card, whatever it
// contains. Confirmed on Scott's machine with CALENDAR_SERVER_DEBUG=1: jot's
// source never appeared in the server's log at all.
//
// So a due date becomes an all-day EVENT. That is a real semantic cost -- a
// deadline is not an appointment -- knowingly accepted, and it is what every
// GTD app that projects to a calendar does. It costs less than a feature that
// quietly does nothing.
//
// ── and BOTH clauses are load-bearing ──────────────────────────────────────
// The second one is the same shape of trap as the first. A calendar source
// that is not SELECTED is as invisible as a task list, and nothing about it
// looks wrong from the outside: the source exists, the events are in it, the
// server just never opens it. `e_source_selectable_set_selected(..., TRUE)` is
// not decoration, and it is read from the source above rather than assumed.
//
// ── why the header has no libecal in it ────────────────────────────────────
// `m_client` and `m_registry` are void*. The whole dependency -- headers,
// types, the pkg-config module -- stops at Desktop.cpp, so a build WITHOUT
// libecal compiles the same Shell code against the same declarations and gets
// `compiled_in() == false`. A header that leaked ECalClient would have put
// `#if JOT_HAS_ECAL` in every caller, which is how an optional dependency stops
// being optional.
//
// ── the calendar is disposable ─────────────────────────────────────────────
// jot creates the source on first use, wipes every row whose uid begins `jot-`
// on every sync, and writes the current set. Nothing is read back. Delete the
// calendar in Evolution or GNOME Calendar and jot loses nothing; untick the box
// and jot removes its rows and leaves the calendar.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

// Which verb produced a result. The Shell needs it because a deferred report
// arrives with no memory of what asked for it, and a completed CLEAR must not
// be allowed to record the fingerprint a pending SYNC was hoping for.
enum class DesktopOp { Sync, Clear };

// What happened, in a form the status line can show and the log can read. An
// error is a MESSAGE, not an exception: EDS being absent, or a user having
// removed the calendar, is a normal state of the world for an optional feature.
struct DesktopResult {
    DesktopOp   op      = DesktopOp::Sync;
    bool        ok      = false;
    // The connect is in flight and the work is queued behind it. NOT a failure
    // and NOT a success -- a third state, and the reason it exists is that the
    // caller must neither record a fingerprint nor report an error yet. A
    // second result with the same `op` follows through the report callback.
    bool        pending = false;
    int         written = 0;
    int         removed = 0;
    std::string message;    // human-readable; empty on a plain success
};

class Desktop {
public:
    Desktop();
    ~Desktop();
    Desktop(const Desktop&)            = delete;
    Desktop& operator=(const Desktop&) = delete;

    // Where a DEFERRED result lands -- the completion of anything that came
    // back `pending`. Called on the main loop, never from a thread, because
    // nothing here runs on one.
    using Report = std::function<void(const DesktopResult&)>;
    void set_report(Report r);

    // Was jot built against libecal at all? Static because the answer is a
    // property of the binary, and the UI has to be able to say "not built with
    // desktop support" without first trying to connect to anything.
    static bool compiled_in();

    // Wipe jot's rows, write these. The whole verb: there is no incremental
    // path, deliberately. A diff against another process's database is state
    // this app would then own two copies of, and rebuilding a few dozen rows
    // costs less than being wrong about which ones moved.
    //
    // Returns `pending` if the client is not up yet; the rows are held and
    // written when it is. A second call while pending REPLACES the held set --
    // the projection is a snapshot of the whole truth, so the newest one is the
    // only one worth writing.
    DesktopResult sync(const std::vector<core::Projected>& items);

    // Remove jot's rows and leave the calendar. What untick does, and what
    // closing a jots folder does -- an empty desktop is honest, a stale one is
    // not.
    DesktopResult clear();

    // Drop the client. If something is in flight, the drop is DEFERRED until it
    // finishes rather than cancelled: untick during a first connect must still
    // get the rows off the desktop, and a cancelled clear would leave last
    // session's todos sitting there with nothing left running that would ever
    // remove them.
    void disconnect();

    // ── THE INSTRUMENT'S DOOR (CANON: the instrument is part of the claim) ──
    // Connect synchronously. THE APP NEVER CALLS THIS -- it exists so that
    // `jot_desktop_probe` can be a straight-line program instead of a main
    // loop, and so that `occurring()` below has something to stand on.
    bool connect_blocking(std::string& err);

    // Run the SAME live query gnome-shell-calendar-server runs -- an
    // `occur-in-time-range?` S-expression over the calendar -- and report each
    // matching row as "<uid>  <summary>".
    //
    // This is the only way to check the claim this milestone rests on without a
    // GNOME session: that a VEVENT jot wrote actually falls inside the window
    // the calendar dropdown asks for. Not used by the app.
    std::vector<std::string> occurring(std::int64_t since, std::int64_t until,
                                       std::string& err);

private:
    // ── outliving the object ───────────────────────────────────────────────
    // An async chain is four callbacks deep and each one is a bare function
    // pointer holding a raw `this`. If the Desktop dies mid-chain -- the app
    // quits during a first connect -- every one of them is a use-after-free.
    //
    // So the callbacks hold a shared_ptr to this box instead, and the
    // destructor nulls `owner`. A callback that arrives late finds a null and
    // returns. It costs one allocation per async call and removes a whole class
    // of crash-on-quit.
    struct Box { Desktop* owner = nullptr; };
    std::shared_ptr<Box> m_box;

    enum class State { Idle, Connecting, Ready };
    State m_state = State::Idle;

    // ECalClient*, ESourceRegistry*, GCancellable*. Opaque -- see the header
    // note above.
    void* m_client   = nullptr;
    void* m_registry = nullptr;
    void* m_cancel   = nullptr;

    Report m_report;

    // What is waiting for the client to come up. `None` is the common case.
    enum class Queued { None, Sync, Clear };
    Queued                       m_queued = Queued::None;
    std::vector<core::Projected> m_queued_items;
    bool                         m_drop_when_done = false;

    // ── the connect chain ──────────────────────────────────────────────────
    // Four D-Bus round trips deep: registry, remove the legacy task list,
    // commit the calendar source, connect the client. Each step is a member
    // taking `void*` so that no libecal type reaches this header; the free
    // GAsyncReadyCallbacks that do the casting live in Desktop.cpp under
    // `DesktopAsync`, which is why it is a friend.
    friend struct DesktopAsync;

    void begin_connect();
    void step_registry_done(void* result);
    void step_legacy_done(void* source, void* result);
    void step_open_or_create();
    void step_commit_done(void* result);
    void step_connect(void* source);
    void step_connect_done(void* result);
    void finish_connect(void* client, const std::string& err);

    void run_queued();
    void drop_client();

    // The shared half of sync() and clear(): delete every component whose uid
    // carries the projection prefix. Returns how many went.
    bool wipe(int& removed, std::string& err);

    DesktopResult sync_now(const std::vector<core::Projected>& items);
    DesktopResult clear_now();
};

}  // namespace jot
