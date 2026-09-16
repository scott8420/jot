// tools/desktop_probe.cpp -- the trace channel for s009, corrected in s010.
//
// I can compile and I can run headless; I cannot see a GNOME calendar dropdown.
// This is the half of the evidence that IS reachable from here: build a fixture
// projection, write it into Evolution Data Server through the real Desktop
// backend, then ask the CALENDAR the SAME live query
// gnome-shell-calendar-server asks -- and see whether jot's rows come back
// inside today's window.
//
// ── s009's version asked the right questions of the wrong thing ────────────
// Every check below is unchanged in substance. They were always the right
// questions; they were pointed at a TASK LIST, which the calendar dropdown
// never opens. The instrument was rigorous and the premise under it was
// unverified, which is how you get confident, worthless evidence.
//
// It proves the mechanism, not the appearance. What it cannot tell you is
// whether the pill reads well, whether it crowds a busy day, or whether
// "Blocked" in a description is the right word on a desktop. That is Scott's
// channel, permanently.
//
// Run it against a throwaway EDS, not your real one:
//
//     export XDG_DATA_HOME=/tmp/probe/data XDG_CONFIG_HOME=/tmp/probe/config
//     export XDG_CACHE_HOME=/tmp/probe/cache
//     dbus-run-session -- ./build/jot_desktop_probe
//
// It wipes and rewrites the "jot" calendar and then clears it, so it leaves
// nothing behind.

#include "Desktop.hpp"
#include "Log.hpp"
#include "core/Nodes.hpp"
#include "core/Projection.hpp"
#include "core/Tasks.hpp"

#include <glib.h>

#include <cstdio>
#include <ctime>
#include <string>

using namespace jot;

namespace {

int checks = 0, failures = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

}  // namespace

int main() {
    log::init();

    if (!Desktop::compiled_in()) {
        std::printf("built without libecal -- nothing to probe\n");
        return 0;
    }

    const std::int64_t now = std::time(nullptr);

    // ── a fixture that covers every branch of the projection rule ───────────
    core::MemoryNodes src;
    const auto project_node = src.create("", "Taxes");
    src.set_body(project_node, "# Taxes\nThe folder on the desk, before the 15th.\n");
    src.set_status(project_node, core::Status::Sequential);

    const auto step1 = src.create(project_node, "Gather the receipts");
    src.make_task(step1, true);
    src.set_due(step1, core::day_end(now));                        // all-day, today

    const auto step2 = src.create(project_node, "Fill the form");
    src.make_task(step2, true);
    src.set_due(step2, core::day_end(now + 86400));                // all-day, tomorrow, BLOCKED

    const auto timed = src.create("", "Call the accountant");
    src.make_task(timed, true);
    src.set_due(timed, now + 3600);                                // timed, in an hour
    src.set_flagged(timed, true);

    const auto overdue = src.create("", "Renew the permit");
    src.make_task(overdue, true);
    src.set_due(overdue, now - 86400 * 3);

    const auto undated = src.create("", "Think about the shed");
    src.make_task(undated, true);                                  // no date -> must NOT project

    const auto finished = src.create("", "Pay the water bill");
    src.make_task(finished, true);
    src.set_due(finished, core::day_end(now));
    src.set_done(finished, true);                                  // done -> must NOT project

    src.create("", "A plain note");                                // not a todo -> must NOT project

    core::TaskIndex tasks;
    tasks.rebuild(src);
    const auto items = core::project(src, tasks, now);

    std::printf("\n-- what the projection decided --\n");
    for (const auto& p : items)
        std::printf("   %-40s all_day=%d overdue=%d start=%lld\n",
                    p.summary.c_str(), p.all_day ? 1 : 0, p.overdue ? 1 : 0,
                    static_cast<long long>(p.start));

    check(items.size() == 4, "four of seven todos project");

    // ── write it, then ask the desktop's own question ───────────────────────
    // The instrument connects SYNCHRONOUSLY so that it can be straight-line
    // code. The app does not; the async path gets its own checks at the bottom,
    // because a connect nothing exercises is a connect that does not work.
    Desktop desk;
    std::string cerr;
    check(desk.connect_blocking(cerr), "connected to the calendar" +
          (cerr.empty() ? std::string{} : "  (" + cerr + ")"));

    const auto r = desk.sync(items);
    std::printf("\n-- sync -- ok=%d written=%d removed=%d %s\n",
                r.ok ? 1 : 0, r.written, r.removed, r.message.c_str());
    check(r.ok, "sync reached Evolution Data Server");
    check(r.written == static_cast<int>(items.size()), "every row was written");

    std::string err;
    const auto today = desk.occurring(core::day_start(now), core::day_end(now), err);
    std::printf("\n-- the calendar server's own query, for today --\n");
    for (const auto& line : today) std::printf("   %s\n", line.c_str());
    if (!err.empty()) std::printf("   error: %s\n", err.c_str());

    bool saw_all_day = false, saw_timed = false, saw_tomorrow = false;
    for (const auto& line : today) {
        if (line.find("Gather the receipts") != std::string::npos) saw_all_day  = true;
        if (line.find("Call the accountant") != std::string::npos) saw_timed    = true;
        if (line.find("Fill the form")       != std::string::npos) saw_tomorrow = true;
    }
    // THE CLAIM OF THE MILESTONE. If either of these fails, nothing appears in
    // the dropdown however clean the build is.
    check(saw_all_day, "an all-day todo falls inside today's window");
    check(saw_timed,   "a timed todo falls inside today's window");
    // The other half, and the one that is easy to forget to ask: the window
    // must also EXCLUDE. A projection that puts everything on every day is a
    // projection nobody reads after the first week.
    check(!saw_tomorrow, "a todo due tomorrow stays out of today's window");

    // A second sync must not double the rows -- the wipe is the whole
    // mechanism, and a projection that accumulates is one that ends up showing
    // every version of a task you ever renamed.
    const auto again = desk.sync(items);
    check(again.removed == static_cast<int>(items.size()),
          "a second sync removes exactly what the first one wrote");

    const auto cleared = desk.clear();
    check(cleared.ok && cleared.removed == static_cast<int>(items.size()),
          "clear() takes jot's rows away again");

    // ── the async connect, which is what the app actually runs ──────────────
    // s009 connected synchronously and the first connect took thirty seconds --
    // confirmed live on Scott's machine, inside the frame that drew the tick.
    // s010 moved it off the main loop, and this is the only place that fact is
    // checked. A cold sync must come back PENDING and must then COMPLETE.
    Desktop      adesk;
    DesktopResult got;
    bool          landed = false;
    adesk.set_report([&](const DesktopResult& res) { got = res; landed = true; });

    const auto cold = adesk.sync(items);
    check(cold.pending, "a cold sync returns pending instead of blocking");
    check(!cold.ok,     "pending is not reported as success");

    // A ceiling, so a hung backend fails the probe instead of hanging it.
    const std::int64_t deadline = std::time(nullptr) + 90;
    while (!landed && std::time(nullptr) < deadline)
        g_main_context_iteration(nullptr, TRUE);

    check(landed, "the queued sync reported back");
    check(got.ok && got.written == static_cast<int>(items.size()),
          "the async connect wrote every row  (" + got.message + ")");

    const auto acleared = adesk.clear();
    check(acleared.ok && acleared.removed == static_cast<int>(items.size()),
          "a connected client clears without queueing");

    std::printf("\n-----------------------------------------------\n");
    std::printf("%d pass / %d fail\n", checks - failures, failures);
    return failures == 0 ? 0 : 1;
}
