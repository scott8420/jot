#include "Desktop.hpp"
#include "Log.hpp"

// Desktop.cpp -- the one file that includes libecal.
//
// Everything below the #if is the real implementation; the #else is a complete,
// honest no-op so that a machine without evolution-data-server-devel still
// builds the whole app and the feature simply reports itself unavailable.
//
// BUILD BOTH HALVES BEFORE SHIPPING. s009 shipped a fallback branch nothing had
// ever compiled and it did not build on Fedora:
//     cmake -B build-noecal -DJOT_DESKTOP=OFF && cmake --build build-noecal

#ifdef JOT_HAS_ECAL

#define HANDLE_LIBICAL_MEMORY
#include <libecal/libecal.h>
#include <libedataserver/libedataserver.h>

#include <ctime>

namespace jot {
namespace {

// The ESource uid. Fixed, so a second run finds the calendar it made last time
// rather than minting a new one each launch -- which is the failure mode that
// leaves a user with fourteen calendars called "jot".
//
// ── why it is not just "jot" ───────────────────────────────────────────────
// That uid is TAKEN, by the task list s009 created on every machine that ran
// it. Reusing it would mean removing that source and creating a new one under
// the same uid in the same breath, and the registry's own view of a removal
// arrives over D-Bus: `e_source_registry_ref_source()` immediately after a
// successful remove can still hand back the source that was just deleted. A
// race whose losing branch is "connect to a task list as if it were a calendar"
// is not worth the tidiness of one uid.
constexpr const char* kSourceUid  = "jot-calendar";
constexpr const char* kLegacyUid  = "jot";           // s009's task list. Removed once.
constexpr const char* kSourceName = "jot";           // what the user sees, and it does not change

// "local-stub" is the parent every on-disk source hangs off in EDS. Named here
// because it is a magic string, and a magic string with no comment is a thing
// nobody dares change later.
constexpr const char* kLocalParent = "local-stub";

// The system timezone's Olson location, for the instrument only.
//
// EDS's own e_cal_system_timezone_get_location() is unreachable from C++ (see
// the note above ical_for), so: TZ if it is set, otherwise what /etc/localtime
// points at, otherwise UTC. Good enough for a probe; NOT good enough to write
// data with, which is why nothing in the write path calls it.
std::string system_tz_location() {
    if (const gchar* tz = g_getenv("TZ"); tz && *tz && *tz != ':') return tz;
    gchar* target = g_file_read_link("/etc/localtime", nullptr);
    if (target) {
        std::string t = target;
        g_free(target);
        const std::string marker = "zoneinfo/";
        if (auto at = t.find(marker); at != std::string::npos)
            return t.substr(at + marker.size());
    }
    return "UTC";
}

std::string take_error(GError*& e, const char* what) {
    std::string msg = what;
    if (e) { msg += ": "; msg += e->message ? e->message : "unknown error"; g_clear_error(&e); }
    return msg;
}

// ── time, and the header that would not link ───────────────────────────────
// The obvious call here is e_cal_system_timezone_get_location(), and it does
// not work from C++: EDS's e-cal-system-timezone.h has no G_BEGIN_DECLS, so the
// declaration picks up C++ linkage and the symbol never resolves. It COMPILES
// and fails at link, which is the expensive order to find out.
//
// Dropping it turned out to be better than working around it, because the two
// cases want different things anyway:
//
//   timed    -> written in UTC. Unambiguous, no zone database involved, and
//               the calendar server reads i_cal_time_is_utc() and converts to
//               local for display. Nothing to get wrong.
//   all-day  -> a DATE has no zone BY DEFINITION; what it needs is the right
//               Y/M/D. So take the calendar date the user meant straight out of
//               localtime_r and set it. Asking a zone object to derive a date
//               from an epoch was the long way round to the same three numbers,
//               and the way that is wrong by a day east of Greenwich.
ICalTime* ical_for(std::int64_t when, bool all_day) {
    if (!all_day)
        return i_cal_time_new_from_timet_with_zone(
            static_cast<time_t>(when), 0, i_cal_timezone_get_utc_timezone());

    std::time_t t = static_cast<std::time_t>(when);
    std::tm     lt{};
    localtime_r(&t, &lt);

    ICalTime* itt = i_cal_time_new_null_date();
    i_cal_time_set_date(itt, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    return itt;
}

// One projected row -> one VEVENT.
//
// s009 wrote VTODOs and nothing ever read them; see Desktop.hpp. A due date is
// now an EVENT, which is a semantic cost paid knowingly.
ICalComponent* to_vevent(const core::Projected& p) {
    ICalComponent* comp = i_cal_component_new_vevent();

    i_cal_component_set_uid(comp, p.uid.c_str());
    i_cal_component_set_summary(comp, p.summary.c_str());
    if (!p.detail.empty()) i_cal_component_set_description(comp, p.detail.c_str());

    ICalTime* start = ical_for(p.start, p.all_day);
    i_cal_component_set_dtstart(comp, start);
    g_clear_object(&start);

    // ── the +1, and why it is not an off-by-one ────────────────────────────
    // THE END IS EXCLUSIVE AND MUST NOT EQUAL THE START. An interval of zero
    // length is not matched by EDS's `occur-in-time-range?` -- the filter the
    // calendar dropdown runs before anything reaches the Shell -- against the
    // day that contains it. The probe caught this exactly in s009: a todo due
    // TOMORROW showed up in today's window (its midnight falls inside today's
    // range) and a todo due TODAY did not. Silently, on the one day that
    // matters.
    //
    // One second past the due instant is the SMALLEST end that makes the
    // interval non-empty, and it reads the same way in both cases because
    // `Projected::due` already carries the right instant for each:
    //
    //   all-day -> due is the last second of the due day, so +1s is midnight
    //              the next morning, which as a DATE is the next day. The event
    //              covers exactly its day -- the native all-day DTEND rule.
    //   timed   -> due is the deadline itself, so the event is one second long.
    //              A deadline is not a block of calendar time and jot does not
    //              invent one; it just has to be non-empty to be seen.
    ICalTime* end = ical_for(p.due + 1, p.all_day);
    i_cal_component_set_dtend(comp, end);
    g_clear_object(&end);

    // NO STATUS PROPERTY. s009 wrote I_CAL_STATUS_NEEDSACTION, which is a VTODO
    // value; a VEVENT's status vocabulary is TENTATIVE / CONFIRMED / CANCELLED
    // and none of the three says anything true about a deadline. The rule
    // already covers it: done todos do not project, so a row that exists is a
    // row still to do.

    // Flagged rides in as priority 1. It is the one jot concept iCalendar has a
    // real field for, and it costs nothing; everything else jot knows is in the
    // description, where it cannot be misread as structure.
    if (p.flagged)
        i_cal_component_take_property(comp, i_cal_property_new_priority(1));

    return comp;
}

// ── the source, and BOTH clauses of the filter ─────────────────────────────
// gnome-shell's calendar-sources.c admits a source through:
//
//     e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR) &&
//     e_source_selectable_get_selected (e_source_get_extension (...))
//
// Enabled matters too -- ESourceRegistryWatcher only ever offers it enabled
// sources. So three things, and a source missing any one of them is invisible
// with nothing about it looking wrong.
void dress_source(ESource* source) {
    e_source_set_parent(source, kLocalParent);
    e_source_set_display_name(source, kSourceName);
    e_source_set_enabled(source, TRUE);

    auto* ext = e_source_get_extension(source, E_SOURCE_EXTENSION_CALENDAR);
    e_source_backend_set_backend_name(E_SOURCE_BACKEND(ext), "local");
    e_source_selectable_set_selected(E_SOURCE_SELECTABLE(ext), TRUE);
}

// Does an existing source already say all three? If not it needs a commit, and
// a source created by an older jot is exactly the case that will not.
bool source_is_dressed(ESource* source) {
    if (!e_source_get_enabled(source)) return false;
    if (!e_source_has_extension(source, E_SOURCE_EXTENSION_CALENDAR)) return false;
    auto* ext = e_source_get_extension(source, E_SOURCE_EXTENSION_CALENDAR);
    return e_source_selectable_get_selected(E_SOURCE_SELECTABLE(ext)) != FALSE;
}

}  // namespace

// ── the async callbacks ────────────────────────────────────────────────────
// Every one of them holds a shared_ptr to the Desktop's Box rather than a raw
// `this`, so a chain still in flight when the app quits finds a null owner and
// returns instead of writing through a dangling pointer.
struct DesktopAsync {
    static gpointer hold(const std::shared_ptr<Desktop::Box>& b) {
        return new std::shared_ptr<Desktop::Box>(b);
    }
    static Desktop* take(gpointer data) {
        auto* sp    = static_cast<std::shared_ptr<Desktop::Box>*>(data);
        Desktop* own = sp && *sp ? (*sp)->owner : nullptr;
        delete sp;
        return own;
    }
    static void on_registry(GObject*, GAsyncResult* res, gpointer data) {
        if (Desktop* d = take(data)) d->step_registry_done(res);
    }
    static void on_legacy(GObject* src, GAsyncResult* res, gpointer data) {
        if (Desktop* d = take(data)) d->step_legacy_done(src, res);
    }
    static void on_commit(GObject*, GAsyncResult* res, gpointer data) {
        if (Desktop* d = take(data)) d->step_commit_done(res);
    }
    static void on_connect(GObject*, GAsyncResult* res, gpointer data) {
        if (Desktop* d = take(data)) d->step_connect_done(res);
    }
};

bool Desktop::compiled_in() { return true; }

Desktop::Desktop() : m_box(std::make_shared<Box>()) { m_box->owner = this; }

Desktop::~Desktop() {
    // Order matters: null the owner FIRST, so any callback that lands between
    // here and the end of the destructor is already inert.
    m_box->owner = nullptr;
    if (m_cancel) {
        g_cancellable_cancel(static_cast<GCancellable*>(m_cancel));
        g_object_unref(static_cast<GCancellable*>(m_cancel));
        m_cancel = nullptr;
    }
    drop_client();
}

void Desktop::set_report(Report r) { m_report = std::move(r); }

void Desktop::drop_client() {
    if (m_client)   { g_object_unref(static_cast<ECalClient*>(m_client));      m_client = nullptr; }
    if (m_registry) { g_object_unref(static_cast<ESourceRegistry*>(m_registry)); m_registry = nullptr; }
    m_state = State::Idle;
}

// ── disconnect defers rather than cancels ──────────────────────────────────
// Untick during a first connect must still get the rows OFF the desktop. If the
// drop cancelled the chain, last session's todos would sit there with nothing
// running that would ever remove them -- and a desktop showing yesterday's
// todos after you turned the feature off is worse than one showing none,
// because you cannot tell by looking that it is stale.
void Desktop::disconnect() {
    if (m_state == State::Connecting || m_queued != Queued::None) {
        m_drop_when_done = true;
        return;
    }
    drop_client();
}

// ─────────────────────────────────────────────────────────────────────────────
// The connect chain. Four D-Bus round trips, none of them on the main loop's
// critical path.
//
// s009 did all of this with the _sync calls and measured THIRTY SECONDS on a
// cold first connect -- confirmed live on Scott's machine, not a sandbox
// artifact. `wait_for_connected_seconds` is a TIMEOUT, not a budget: it returns
// as soon as the backend is up and gives up after that long. Passing 0 was
// tried in s009 and is worse, not better -- the client comes back immediately
// and unusable and the first read then blocks with no timeout at all.
//
// So the wait stays and the CALL moves off the main loop instead. Not a thread:
// every one of these has an async form, and a thread would mean owning a second
// context for a problem the library already solved.
// ─────────────────────────────────────────────────────────────────────────────
void Desktop::begin_connect() {
    if (m_state != State::Idle) return;
    m_state = State::Connecting;
    if (!m_cancel) m_cancel = g_cancellable_new();
    e_source_registry_new(static_cast<GCancellable*>(m_cancel),
                          DesktopAsync::on_registry, DesktopAsync::hold(m_box));
}

void Desktop::step_registry_done(void* result) {
    GError* e = nullptr;
    ESourceRegistry* reg =
        e_source_registry_new_finish(static_cast<GAsyncResult*>(result), &e);
    if (!reg) { finish_connect(nullptr, take_error(e, "no source registry")); return; }
    m_registry = reg;

    // ── the one-time cleanup ───────────────────────────────────────────────
    // s009 created a TASK LIST with uid "jot" on every machine that ran it.
    // Nothing writes to it now and nothing ever read it, so leaving it behind
    // means the user keeps a task list called "jot" that quietly does nothing
    // -- which is exactly the state this milestone exists to end.
    ESource* legacy = e_source_registry_ref_source(reg, kLegacyUid);
    if (legacy && e_source_has_extension(legacy, E_SOURCE_EXTENSION_TASK_LIST) &&
        !e_source_has_extension(legacy, E_SOURCE_EXTENSION_CALENDAR) &&
        e_source_get_removable(legacy)) {
        if (auto lg = log::get(log::Area::Desktop))
            lg->info("removing s009's leftover '{}' task list", kLegacyUid);
        e_source_remove(legacy, static_cast<GCancellable*>(m_cancel),
                        DesktopAsync::on_legacy, DesktopAsync::hold(m_box));
        g_object_unref(legacy);
        return;
    }
    g_clear_object(&legacy);
    step_open_or_create();
}

void Desktop::step_legacy_done(void* source, void* result) {
    GError* e = nullptr;
    // A failure here is not fatal to anything. The old list is dead weight, not
    // a dependency; if it will not go, say so once and get on with the work.
    if (!e_source_remove_finish(static_cast<ESource*>(source),
                                static_cast<GAsyncResult*>(result), &e)) {
        if (auto lg = log::get(log::Area::Desktop))
            lg->warn("could not remove the old task list: {}",
                     e && e->message ? e->message : "unknown");
    }
    g_clear_error(&e);
    step_open_or_create();
}

void Desktop::step_open_or_create() {
    auto* reg = static_cast<ESourceRegistry*>(m_registry);
    GError*  e      = nullptr;
    ESource* source = e_source_registry_ref_source(reg, kSourceUid);

    if (source && source_is_dressed(source)) { step_connect(source); return; }

    if (!source) {
        source = e_source_new_with_uid(kSourceUid, nullptr, &e);
        if (!source) { finish_connect(nullptr, take_error(e, "could not make a calendar")); return; }
        if (auto lg = log::get(log::Area::Desktop))
            lg->info("creating the '{}' calendar", kSourceName);
    } else if (auto lg = log::get(log::Area::Desktop)) {
        // An existing source missing `selected` is the s009 trap in miniature:
        // present, correct-looking, and invisible to the dropdown.
        lg->info("the '{}' calendar needed re-registering", kSourceName);
    }

    dress_source(source);
    e_source_registry_commit_source(reg, source, static_cast<GCancellable*>(m_cancel),
                                    DesktopAsync::on_commit, DesktopAsync::hold(m_box));
    g_object_unref(source);
}

void Desktop::step_commit_done(void* result) {
    auto*   reg = static_cast<ESourceRegistry*>(m_registry);
    GError* e   = nullptr;
    if (!e_source_registry_commit_source_finish(reg, static_cast<GAsyncResult*>(result), &e)) {
        finish_connect(nullptr, take_error(e, "could not register the calendar"));
        return;
    }
    // Re-ref rather than reuse: commit hands the source to the registry
    // service, and the object the registry now holds is the one to connect to.
    ESource* source = e_source_registry_ref_source(reg, kSourceUid);
    if (!source) { finish_connect(nullptr, "the calendar vanished after registering"); return; }
    step_connect(source);
}

// Takes ownership of `source`.
void Desktop::step_connect(void* source) {
    auto* src = static_cast<ESource*>(source);
    e_cal_client_connect(src, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 30,
                         static_cast<GCancellable*>(m_cancel),
                         DesktopAsync::on_connect, DesktopAsync::hold(m_box));
    g_object_unref(src);
}

void Desktop::step_connect_done(void* result) {
    GError*  e      = nullptr;
    EClient* client = e_cal_client_connect_finish(static_cast<GAsyncResult*>(result), &e);
    if (!client) { finish_connect(nullptr, take_error(e, "could not open the calendar")); return; }
    finish_connect(client, {});
}

void Desktop::finish_connect(void* client, const std::string& err) {
    if (client) {
        m_client = client;
        m_state  = State::Ready;
        if (auto lg = log::get(log::Area::Desktop)) lg->info("connected to the desktop calendar");
        run_queued();
        return;
    }

    m_state = State::Idle;
    if (m_registry) { g_object_unref(static_cast<ESourceRegistry*>(m_registry)); m_registry = nullptr; }

    // The queued work cannot happen, and the caller is holding a `pending` that
    // will never resolve unless it is told. Report the failure under the op
    // that asked for it.
    const Queued q = m_queued;
    m_queued = Queued::None;
    m_queued_items.clear();
    m_drop_when_done = false;
    if (q != Queued::None && m_report) {
        DesktopResult r;
        r.op      = (q == Queued::Clear) ? DesktopOp::Clear : DesktopOp::Sync;
        r.message = err;
        m_report(r);
    }
}

void Desktop::run_queued() {
    const Queued q = m_queued;
    m_queued = Queued::None;
    if (q == Queued::None) { if (m_drop_when_done) { m_drop_when_done = false; drop_client(); } return; }

    DesktopResult r = (q == Queued::Clear) ? clear_now() : sync_now(m_queued_items);
    m_queued_items.clear();
    if (m_drop_when_done) { m_drop_when_done = false; drop_client(); }
    if (m_report) m_report(r);
}

// ─────────────────────────────────────────────────────────────────────────────
// The two verbs. Each is either done now, or queued behind the connect.
// ─────────────────────────────────────────────────────────────────────────────
DesktopResult Desktop::sync(const std::vector<core::Projected>& items) {
    if (m_state == State::Ready) return sync_now(items);

    // A second sync while connecting REPLACES the held set. The projection is a
    // snapshot of the whole truth, so the newest one is the only one worth
    // writing; queueing both would write the stale one first for no reason.
    m_queued       = Queued::Sync;
    m_queued_items = items;
    if (m_state == State::Idle) begin_connect();

    DesktopResult r;
    r.op      = DesktopOp::Sync;
    r.pending = true;
    r.message = "Connecting to the desktop\u2026";
    return r;
}

DesktopResult Desktop::clear() {
    if (m_state == State::Ready) return clear_now();

    // A clear SUPERSEDES a queued sync. Turning the feature off while its first
    // write is still queued must not go on to do the write.
    m_queued = Queued::Clear;
    m_queued_items.clear();
    if (m_state == State::Idle) begin_connect();

    DesktopResult r;
    r.op      = DesktopOp::Clear;
    r.pending = true;
    r.message = "Turning it off\u2026";
    return r;
}

// "#t" is EDS's match-everything S-expression. We ask for EVERY component and
// filter by uid prefix ourselves rather than asking the backend to match on
// uid: the prefix is jot's convention, not the query language's, and a filter
// written in two dialects is a filter that disagrees with itself.
bool Desktop::wipe(int& removed, std::string& err) {
    removed = 0;
    auto* client = static_cast<ECalClient*>(m_client);

    GError* e     = nullptr;
    GSList* comps = nullptr;
    if (!e_cal_client_get_object_list_sync(client, "#t", &comps, nullptr, &e)) {
        err = take_error(e, "could not read the calendar");
        return false;
    }

    const std::string prefix = core::projection_prefix();
    for (GSList* l = comps; l; l = l->next) {
        auto* comp = static_cast<ICalComponent*>(l->data);
        if (!comp) continue;
        const gchar* uid = i_cal_component_get_uid(comp);
        if (!uid || prefix.compare(0, prefix.size(), uid, 0, prefix.size()) != 0) continue;

        GError* re = nullptr;
        if (e_cal_client_remove_object_sync(client, uid, nullptr, E_CAL_OBJ_MOD_ALL,
                                            E_CAL_OPERATION_FLAG_NONE, nullptr, &re)) {
            ++removed;
        } else {
            // One stubborn row must not abort the rebuild: a projection that
            // gives up halfway leaves the desktop in a state neither jot nor
            // the user asked for. Log it, keep going.
            if (auto lg = log::get(log::Area::Desktop))
                lg->warn("could not remove '{}': {}", uid,
                         re && re->message ? re->message : "unknown");
            g_clear_error(&re);
        }
    }
    g_slist_free_full(comps, g_object_unref);
    return true;
}

DesktopResult Desktop::sync_now(const std::vector<core::Projected>& items) {
    DesktopResult r;
    r.op = DesktopOp::Sync;
    if (!wipe(r.removed, r.message)) { drop_client(); return r; }

    auto* client = static_cast<ECalClient*>(m_client);
    for (const auto& p : items) {
        ICalComponent* comp = to_vevent(p);
        GError* e    = nullptr;
        gchar*  uid  = nullptr;
        if (e_cal_client_create_object_sync(client, comp, E_CAL_OPERATION_FLAG_NONE,
                                            &uid, nullptr, &e)) {
            ++r.written;
            g_free(uid);
        } else {
            if (auto lg = log::get(log::Area::Desktop))
                lg->warn("could not write '{}': {}", p.uid,
                         e && e->message ? e->message : "unknown");
            g_clear_error(&e);
        }
        g_object_unref(comp);
    }

    r.ok = true;
    if (r.written != static_cast<int>(items.size()))
        r.message = "some todos did not reach the desktop";
    if (auto lg = log::get(log::Area::Desktop))
        lg->info("sync wrote {} of {}, removed {}", r.written, items.size(), r.removed);
    return r;
}

DesktopResult Desktop::clear_now() {
    DesktopResult r;
    r.op = DesktopOp::Clear;
    if (!wipe(r.removed, r.message)) { drop_client(); return r; }
    r.ok = true;
    if (auto lg = log::get(log::Area::Desktop)) lg->info("cleared {} rows", r.removed);
    return r;
}

// ── the instrument's door ──────────────────────────────────────────────────
// The synchronous chain, so `jot_desktop_probe` can be straight-line code. THE
// APP NEVER CALLS THIS. It is the same four steps in the same order, and that
// is deliberate: an instrument that connected differently would be testing a
// different program.
bool Desktop::connect_blocking(std::string& err) {
    if (m_state == State::Ready) return true;

    GError* e = nullptr;
    auto* reg = static_cast<ESourceRegistry*>(m_registry);
    if (!reg) {
        reg = e_source_registry_new_sync(nullptr, &e);
        if (!reg) { err = take_error(e, "no source registry"); return false; }
        m_registry = reg;
    }

    ESource* legacy = e_source_registry_ref_source(reg, kLegacyUid);
    if (legacy && e_source_has_extension(legacy, E_SOURCE_EXTENSION_TASK_LIST) &&
        !e_source_has_extension(legacy, E_SOURCE_EXTENSION_CALENDAR) &&
        e_source_get_removable(legacy)) {
        GError* re = nullptr;
        e_source_remove_sync(legacy, nullptr, &re);
        g_clear_error(&re);
    }
    g_clear_object(&legacy);

    ESource* source = e_source_registry_ref_source(reg, kSourceUid);
    if (!source || !source_is_dressed(source)) {
        if (!source) {
            source = e_source_new_with_uid(kSourceUid, nullptr, &e);
            if (!source) { err = take_error(e, "could not make a calendar"); return false; }
        }
        dress_source(source);
        if (!e_source_registry_commit_source_sync(reg, source, nullptr, &e)) {
            err = take_error(e, "could not register the calendar");
            g_object_unref(source);
            return false;
        }
        g_object_unref(source);
        source = e_source_registry_ref_source(reg, kSourceUid);
        if (!source) { err = "the calendar vanished after registering"; return false; }
    }

    EClient* client = e_cal_client_connect_sync(
        source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 30, nullptr, &e);
    g_object_unref(source);
    if (!client) { err = take_error(e, "could not open the calendar"); return false; }

    m_client = client;
    m_state  = State::Ready;
    return true;
}

std::vector<std::string> Desktop::occurring(std::int64_t since, std::int64_t until,
                                            std::string& err) {
    std::vector<std::string> out;
    if (!connect_blocking(err)) return out;

    // The query is copied from gnome-shell-calendar-server's app_start_view(),
    // shape and all, INCLUDING the timezone location argument. A query written
    // "equivalently" would be a second dialect of the thing being tested.
    // SET THE DEFAULT ZONE FIRST, as app_start_view() does. This is not
    // decoration: a DATE value carries no zone, and the client resolves it
    // against its default -- which starts as UTC. Without this line an all-day
    // row due TOMORROW matches TODAY's window everywhere west of Greenwich,
    // because tomorrow's UTC midnight is still today locally. The probe showed
    // exactly that before this line existed.
    const std::string loc  = system_tz_location();
    ICalTimezone*     zone = i_cal_timezone_get_builtin_timezone(loc.c_str());
    if (!zone) zone = i_cal_timezone_get_utc_timezone();
    e_cal_client_set_default_timezone(static_cast<ECalClient*>(m_client), zone);

    gchar* since_s = isodate_from_time_t(static_cast<time_t>(since));
    gchar* until_s = isodate_from_time_t(static_cast<time_t>(until));
    gchar* sexp    = g_strdup_printf(
        "occur-in-time-range? (make-time \"%s\") (make-time \"%s\") \"%s\"",
        since_s, until_s, loc.c_str());

    GError* e     = nullptr;
    GSList* comps = nullptr;
    if (e_cal_client_get_object_list_sync(static_cast<ECalClient*>(m_client), sexp,
                                          &comps, nullptr, &e)) {
        for (GSList* l = comps; l; l = l->next) {
            auto* c = static_cast<ICalComponent*>(l->data);
            if (!c) continue;

            // ── THE SECOND FILTER ──────────────────────────────────────────
            // EDS's occur-in-time-range? is not the last word, and assuming it
            // was is what made this instrument disagree with reality. The
            // calendar server re-filters everything the query returns by the
            // component's own DTSTART, in app_notify_events_added():
            //
            //     (start >= since && start < until) ||
            //     (start <= since && end - 1 > since)
            //
            // So the backend over-returning is harmless -- what actually
            // decides which day a row lands on is DTSTART, resolved exactly
            // the way get_ical_start_time() resolves it. Replicating only half
            // the pipeline is replicating the wrong half.
            ICalTime* dt = i_cal_component_get_dtstart(c);
            if (!dt) continue;
            const bool    is_date = i_cal_time_is_date(dt) != 0;
            ICalTimezone* dz      = i_cal_time_is_utc(dt) ? i_cal_timezone_get_utc_timezone() : zone;
            i_cal_time_set_timezone(dt, dz);
            const std::int64_t start = i_cal_time_as_timet_with_zone(dt, dz);
            g_clear_object(&dt);

            const std::int64_t end = is_date ? start + 86400 : start;
            const bool inside = (start >= since && start < until) ||
                                (start <= since && (end - 1) > since);
            if (!inside) continue;

            const gchar* uid = i_cal_component_get_uid(c);
            const gchar* sum = i_cal_component_get_summary(c);
            out.push_back(std::string(uid ? uid : "?") + "  " + (sum ? sum : ""));
        }
        g_slist_free_full(comps, g_object_unref);
    } else {
        err = take_error(e, "live query failed");
    }

    g_free(sexp);
    g_free(since_s);
    g_free(until_s);
    return out;
}

}  // namespace jot

#else  // ── built without libecal ──────────────────────────────────────────────

namespace jot {

namespace {
// The one sentence this whole half exists to say. Written once so the two
// callers cannot drift -- s009's fallback branch had the SAME implementation
// pasted at two sites and one of them was wrong, and nothing ever compiled it.
constexpr const char* kNoEcal =
    "this build has no desktop support (libecal was missing at build time)";
}  // namespace

bool Desktop::compiled_in() { return false; }

Desktop::Desktop() : m_box(std::make_shared<Box>()) { m_box->owner = this; }
Desktop::~Desktop() { m_box->owner = nullptr; }

void Desktop::set_report(Report r) { m_report = std::move(r); }
void Desktop::disconnect() {}
void Desktop::drop_client() {}
void Desktop::begin_connect() {}
void Desktop::step_registry_done(void*) {}
void Desktop::step_legacy_done(void*, void*) {}
void Desktop::step_open_or_create() {}
void Desktop::step_commit_done(void*) {}
void Desktop::step_connect(void*) {}
void Desktop::step_connect_done(void*) {}
void Desktop::finish_connect(void*, const std::string&) {}
void Desktop::run_queued() {}
bool Desktop::wipe(int&, std::string& err) { err = kNoEcal; return false; }

bool Desktop::connect_blocking(std::string& err) { err = kNoEcal; return false; }

DesktopResult Desktop::sync_now(const std::vector<core::Projected>&) {
    DesktopResult r;
    r.op = DesktopOp::Sync;
    r.message = kNoEcal;
    return r;
}
DesktopResult Desktop::clear_now() {
    DesktopResult r;
    r.op = DesktopOp::Clear;
    r.message = kNoEcal;
    return r;
}
DesktopResult Desktop::sync(const std::vector<core::Projected>& items) { return sync_now(items); }
DesktopResult Desktop::clear() { return clear_now(); }

std::vector<std::string> Desktop::occurring(std::int64_t, std::int64_t, std::string& err) {
    err = kNoEcal;
    return {};
}

}  // namespace jot

#endif
