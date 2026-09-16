#pragma once
#include <functional>
#include <memory>
#include <string>

#include <giomm/dbusconnection.h>
#include <glibmm/refptr.h>

// ─────────────────────────────────────────────────────────────────────────────
// Notifier -- the only file in jot that puts a notification on the bus, and the
// one thing it does that `g_application_send_notification()` cannot: come back
// and say whether it arrived.
//
// ── why not GApplication ───────────────────────────────────────────────────
// `g_application_send_notification()` returns void. Not a bool, not a GError**,
// not an async pair -- there is no failure it is able to report. Every send
// through it looks the same:
//
//   * the one the daemon accepted,
//   * the one the daemon DROPPED because it could not look the application id
//     up among the installed desktop entries (silent, by design, upstream),
//   * and the one that was never made at all, because a hidden window returned
//     null from get_application() and the guard swallowed it (s012, an evening).
//
// `org.gtk.Notifications.AddNotification(s app_id, s id, a{sv} notification)`
// is the SAME REQUEST -- it is literally what GLib's own "gtk" notification
// backend sends -- but it is a D-Bus method call, so it has a reply. The reply
// is the receipt, and the receipt is the whole milestone.
//
// ── what this is NOT ───────────────────────────────────────────────────────
// It is not proof a human saw anything. It is proof the daemon took it: the
// application id resolved, the dict deserialized, the row is on its list. That
// is exactly one channel of the two (RULES: convergent evidence), and it is the
// channel that was missing entirely.
//
// ── the dict is built by hand, and faithfully ──────────────────────────────
// The keys below are the ones `g_notification_serialize()` writes, in the same
// shapes: an icon is a SERIALIZED GIcon (g_icon_serialize, not a bare name we
// guessed at), a priority is one of four strings, and a default action's target
// is the action's parameter as a variant. A hand-rolled shape the daemon's
// g_icon_deserialize rejects shows up as a missing icon and nothing else -- so
// the real function builds it.
//
// ── and it is not the ONLY path ────────────────────────────────────────────
// org.gtk.Notifications is GNOME's (it is gnome-shell that owns the name). On a
// desktop that has no such service the call fails with SERVICE_UNKNOWN, and
// that is not the same answer as "refused" -- it means there is no receipted
// road at all, and the caller should take the unreceipted one rather than sulk.
// So the outcome is three-valued, and the policy for the third belongs to the
// Shell, not here. (CANON: refuse on a whole answer, warn on a partial one.)
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

// What jot asks the daemon to show. Flat, and deliberately not a
// Gio::Notification: everything here has to survive the round trip so that a
// receipt can carry back what was attempted.
struct Notice {
    std::string key;        // OUR identity -- the announce key. Comes back untouched.
    std::string tray_id;    // the DAEMON's identity for the row: same id replaces, never stacks
    std::string title;
    std::string body;
    std::string icon;       // themed icon name, or empty
    std::string action;     // "app.goto-node", or empty
    std::string target;     // that action's string parameter
    bool        urgent = false;
};

// What came back. `notice` is the attempt itself, carried home so the caller can
// take a second road without keeping a table of what is in flight.
struct Receipt {
    enum class Outcome {
        Delivered,   // the daemon has it. The only outcome that is EVIDENCE.
        Refused,     // it answered, and the answer was no. Worth retrying.
        NoService,   // nothing owns org.gtk.Notifications here. Not a failure of ours.
    };

    Notice      notice;
    Outcome     outcome = Outcome::Refused;
    std::string message;   // the error text, verbatim -- never a paraphrase
};

class Notifier {
public:
    explicit Notifier(std::string app_id);
    ~Notifier();
    Notifier(const Notifier&)            = delete;
    Notifier& operator=(const Notifier&) = delete;

    // Where every receipt lands. On the main loop; nothing here runs on a thread.
    using Reply = std::function<void(const Receipt&)>;
    void set_reply(Reply r);

    // Is there a session bus at all? False in a sandbox, in a tty, under a
    // cron job -- and then every send answers NoService without a round trip.
    bool bus_ok() const;

    // Ask. Returns nothing: the answer is the receipt, and it arrives later.
    void send(const Notice& n);

private:
    // ── outliving the object (the Desktop pattern, same reason) ────────────
    // A reply can arrive after the Shell that owns this is gone. The callback
    // holds a shared_ptr to this box instead of `this`, and the destructor
    // nulls `owner`; a late reply finds a null and returns.
    struct Box { Notifier* owner = nullptr; };
    std::shared_ptr<Box> m_box;

    std::string                         m_app_id;
    Glib::RefPtr<Gio::DBus::Connection> m_bus;
    Reply                               m_reply;

    void deliver(const Receipt& r);
};

}  // namespace jot
