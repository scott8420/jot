#include "Notifier.hpp"
#include "Log.hpp"

#include <gio/gio.h>

#include <giomm/asyncresult.h>
#include <glibmm/error.h>
#include <glibmm/variant.h>

#include <utility>

namespace jot {
namespace {

constexpr const char* kPath  = "/org/gtk/Notifications";
constexpr const char* kIface = "org.gtk.Notifications";
constexpr const char* kName  = "org.gtk.Notifications";

// The a{sv} GLib's own gtk backend sends, assembled with the C API because the
// value type is `v` and every entry holds a different one. Built floating and
// sunk by the VariantContainerBase constructor -- one reference, owned here.
Glib::VariantContainerBase build(const std::string& app_id, const Notice& n) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));

    g_variant_builder_add(&b, "{sv}", "title", g_variant_new_string(n.title.c_str()));
    if (!n.body.empty())
        g_variant_builder_add(&b, "{sv}", "body", g_variant_new_string(n.body.c_str()));

    if (!n.icon.empty()) {
        // g_icon_serialize, not a bare string. For a themed icon the result
        // happens to BE a string today -- but the daemon calls
        // g_icon_deserialize on whatever arrives, and a shape we guessed at is
        // rejected silently, which is indistinguishable from an icon that does
        // not resolve. Let the real function decide the shape.
        GIcon* icon = g_themed_icon_new(n.icon.c_str());
        if (GVariant* sv = g_icon_serialize(icon))
            g_variant_builder_add(&b, "{sv}", "icon", sv);
        g_object_unref(icon);
    }

    // One of exactly four strings. Urgent is what survives Do Not Disturb, and
    // an overdue deadline has earned it; a deadline arriving on schedule has not.
    g_variant_builder_add(&b, "{sv}", "priority",
                          g_variant_new_string(n.urgent ? "urgent" : "normal"));

    if (!n.action.empty()) {
        g_variant_builder_add(&b, "{sv}", "default-action",
                              g_variant_new_string(n.action.c_str()));
        // The target is the action's PARAMETER, stored as the variant itself --
        // not a string containing one. app.goto-node takes an `s`, so this is
        // the same variant the accelerator path would build.
        if (!n.target.empty())
            g_variant_builder_add(&b, "{sv}", "default-action-target",
                                  g_variant_new_string(n.target.c_str()));
    }

    GVariant* params = g_variant_new("(ss@a{sv})", app_id.c_str(), n.tray_id.c_str(),
                                     g_variant_builder_end(&b));
    return Glib::VariantContainerBase(params, false);
}

// "Nobody is listening" is not "it said no". The first means take the other
// road; the second means ask again in a minute. Told apart by the D-Bus error
// code rather than by reading the message, because the message is prose and
// prose is translated.
bool no_service(const Glib::Error& e) {
    return e.domain() == G_DBUS_ERROR &&
           (e.code() == G_DBUS_ERROR_SERVICE_UNKNOWN ||
            e.code() == G_DBUS_ERROR_NAME_HAS_NO_OWNER);
}

}  // namespace

Notifier::Notifier(std::string app_id)
    : m_box(std::make_shared<Box>()), m_app_id(std::move(app_id)) {
    m_box->owner = this;
    try {
        m_bus = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::SESSION);
    } catch (const Glib::Error& e) {
        // No session bus. Normal in a tty or a container, and the only cost is
        // that every send answers NoService immediately.
        if (auto lg = log::get(log::Area::Shell))
            lg->warn("notifier: no session bus -- {}", e.what());
    }
}

Notifier::~Notifier() { m_box->owner = nullptr; }

void Notifier::set_reply(Reply r) { m_reply = std::move(r); }

bool Notifier::bus_ok() const { return static_cast<bool>(m_bus); }

void Notifier::deliver(const Receipt& r) {
    if (m_reply) m_reply(r);
}

void Notifier::send(const Notice& n) {
    if (!m_bus) {
        Receipt r{n, Receipt::Outcome::NoService, "no session bus"};
        deliver(r);
        return;
    }

    // The connection is captured BY VALUE alongside the box: a reply that
    // arrives after this Notifier is gone still has to be finished, and
    // call_finish is the connection's method, not ours.
    auto box = m_box;
    auto bus = m_bus;

    m_bus->call(
        kPath, kIface, "AddNotification", build(m_app_id, n),
        [box, bus, n](Glib::RefPtr<Gio::AsyncResult>& res) {
            Receipt r{n, Receipt::Outcome::Delivered, {}};
            try {
                bus->call_finish(res);          // throws, or the daemon has it
            } catch (const Glib::Error& e) {
                r.outcome = no_service(e) ? Receipt::Outcome::NoService
                                          : Receipt::Outcome::Refused;
                r.message = e.what();
            }
            if (Notifier* self = box->owner) self->deliver(r);
        },
        kName);
}

}  // namespace jot
