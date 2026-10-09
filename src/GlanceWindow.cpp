#include "GlanceWindow.hpp"
#include "Log.hpp"
#include "Registry.hpp"

#include <gdkmm/clipboard.h>
#include <gdkmm/contentprovider.h>
#include <glibmm/bytes.h>
#include <glibmm/miscutils.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/filelauncher.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/widgetpaintable.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/urilauncher.h>
#include <gdk/gdkkeysyms.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

// GlanceWindow.cpp -- draws core::glance() and hands it on. See the header.

namespace jot {

namespace {

std::int64_t now_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// "Wednesday 7 October" -- the card's big line.
std::string long_day(std::int64_t when) {
    const std::time_t t = static_cast<std::time_t>(when);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof buf, "%A %e %B", &tm);
    std::string out = buf;
    if (auto p = out.find("  "); p != std::string::npos) out.erase(p, 1);
    return out;
}

const char* kind_class(core::GlanceKind k) {
    switch (k) {
        case core::GlanceKind::Late:     return "jot-glance-late";
        case core::GlanceKind::DueToday: return "jot-glance-due";
        case core::GlanceKind::Short:    return "jot-glance-short";
        case core::GlanceKind::Starts:   return "jot-glance-starts";
        case core::GlanceKind::Flagged:  return "jot-glance-flagged";
        case core::GlanceKind::Errands:  return "jot-glance-errands";
    }
    return "";
}

void log_info(const std::string& s) {
    if (auto lg = log::get(log::Area::Shell)) lg->info("glance: {}", s);
}

}  // namespace

GlanceWindow::GlanceWindow()
    : m_day("glance.day"),
      m_summary("glance.summary"),
      m_scroll("glance.scroll"),
      m_column("glance.column", Gtk::Orientation::VERTICAL, 0),
      m_to("glance.to"),
      m_status("glance.status"),
      m_copy("glance.copy"),
      m_email("glance.email"),
      m_save("glance.save"),
      m_chip("glance.chip", Gtk::Orientation::HORIZONTAL, 6),
      m_chip_name("glance.chip.name") {
    set_name("shell.glance");
    registry::add("shell.glance", this);
    set_title("Glance at Today");
    set_modal(false);
    set_resizable(true);
    set_default_size(460, 720);
    set_hide_on_close(true);
    build();
}

GlanceWindow::~GlanceWindow() { registry::remove(this); }

void GlanceWindow::build() {
    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(true);

    // The three ways out, joined -- one idea, "send it on", one control.
    auto* out = Gtk::make_managed<widgets::Box>(widgets::unregistered, "glance.out",
                                                Gtk::Orientation::HORIZONTAL, 0);
    out->add_css_class("linked");
    auto dress = [](widgets::Button& b, const char* icon, const char* words, const char* tip) {
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto* img = Gtk::make_managed<Gtk::Image>();
        img->set_from_icon_name(icon);
        box->append(*img);
        box->append(*Gtk::make_managed<Gtk::Label>(words));
        b.set_child(*box);
        b.set_tooltip_text(tip);
    };
    dress(m_copy, "edit-copy-symbolic", "Copy",
          "Copy the Glance -- paste it into Keep, Notes or a message");
    dress(m_email, "mail-send-symbolic", "Email",
          "A new mail with the Glance in it, to the address below");
    dress(m_save, "x-office-calendar-symbolic", "Save .ics",
          "Save the day as a calendar file -- Google Calendar's Import, or the iPhone's Mail, takes it");
    m_copy.signal_clicked().connect(sigc::mem_fun(*this, &GlanceWindow::on_copy));
    m_email.signal_clicked().connect(sigc::mem_fun(*this, &GlanceWindow::on_email));
    m_save.signal_clicked().connect(sigc::mem_fun(*this, &GlanceWindow::on_save));
    out->append(m_copy);
    out->append(m_email);
    out->append(m_save);
    hb->pack_end(*out);
    set_titlebar(*hb);

    auto* page = Gtk::make_managed<widgets::Box>(widgets::unregistered, "glance.page",
                                                 Gtk::Orientation::VERTICAL, 0);
    // The card's head: the day, large; what it adds up to, quiet.
    auto* head = Gtk::make_managed<widgets::Box>(widgets::unregistered, "glance.head",
                                                 Gtk::Orientation::VERTICAL, 2);
    head->set_margin_start(22);
    head->set_margin_end(22);
    head->set_margin_top(18);
    head->set_margin_bottom(8);
    m_day.add_css_class("jot-glance-day");
    m_day.set_xalign(0);
    m_summary.add_css_class("dim-label");
    m_summary.set_xalign(0);
    head->append(m_day);
    head->append(m_summary);
    page->append(*head);

    m_column.set_margin_start(14);
    m_column.set_margin_end(14);
    m_column.set_margin_bottom(14);
    m_scroll.set_child(m_column);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    page->append(m_scroll);

    // The foot: who Email writes to, and what the last action did.
    auto* foot = Gtk::make_managed<widgets::Box>(widgets::unregistered, "glance.foot",
                                                 Gtk::Orientation::VERTICAL, 6);
    foot->add_css_class("jot-glance-foot");
    foot->set_margin_start(16);
    foot->set_margin_end(16);
    foot->set_margin_top(8);
    foot->set_margin_bottom(12);
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* to_lbl = Gtk::make_managed<Gtk::Label>("Email to");
    to_lbl->add_css_class("dim-label");
    m_to.set_placeholder_text("you@gmail.com -- or leave empty and the mail asks");
    m_to.set_hexpand(true);
    m_to.signal_changed().connect([this]() { m_sig_prefs.emit(m_to.get_text(), m_save_dir); });
    row->append(*to_lbl);
    row->append(m_to);
    foot->append(*row);

    // s066b: the day's .ics as a file to drag into a mail.
    auto* chips = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* cal = Gtk::make_managed<Gtk::Image>();
    cal->set_from_icon_name("x-office-calendar-symbolic");
    m_chip.append(*cal);
    m_chip.append(m_chip_name);
    m_chip.add_css_class("jot-glance-chip");
    m_chip.set_tooltip_text("Drag onto the mail you are writing to attach the day's calendar file. "
                            "Click to show it in Files.");
    m_chip.set_cursor(Gdk::Cursor::create("grab"));
    auto* chip_hint = Gtk::make_managed<Gtk::Label>("\u2190 drag into the mail to attach");
    chip_hint->add_css_class("dim-label");
    chip_hint->add_css_class("caption");
    chips->append(m_chip);
    chips->append(*chip_hint);
    // s071: the "?" -- the guide's Glance page. In the foot, at the right: the
    // title bar is too narrow to spare it (the title was cut to "Glance of T...").
    {
        chip_hint->set_hexpand(true);
        chip_hint->set_xalign(0);
        auto* q = Gtk::make_managed<widgets::Button>(widgets::unregistered, "glance.help", "?");
        q->add_css_class("jot-help-q");
        q->set_valign(Gtk::Align::CENTER);
        q->set_tooltip_text("Help for the Glance -- its page in the jot guide");
        q->signal_clicked().connect([this]() { m_sig_help.emit(); });
        chips->append(*q);
    }
    foot->append(*chips);

    auto drag = Gtk::DragSource::create();
    drag->set_actions(Gdk::DragAction::COPY);
    drag->signal_prepare().connect([this](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
        const std::string path = write_drag_file();
        if (path.empty()) return {};
        // A FILE LIST, as Files offers one: GTK serialises it to text/uri-list
        // (and the portal's form), which browsers and mail apps take as a file.
        GFile* f = g_file_new_for_path(path.c_str());
        GSList* l = g_slist_append(nullptr, f);
        GdkFileList* fl = gdk_file_list_new_from_list(l);
        GValue v = G_VALUE_INIT;
        g_value_init(&v, GDK_TYPE_FILE_LIST);
        g_value_take_boxed(&v, fl);
        GdkContentProvider* cp = gdk_content_provider_new_for_value(&v);
        g_value_unset(&v);
        g_slist_free(l);
        g_object_unref(f);
        log_info("drag " + path);
        return Glib::wrap(cp);
    }, false);
    drag->signal_drag_begin().connect([this, drag](const Glib::RefPtr<Gdk::Drag>&) {
        m_dropped = false;
        drag->set_icon(Gtk::WidgetPaintable::create(m_chip), 0, 0);
    });
    drag->signal_drag_cancel().connect([this](const Glib::RefPtr<Gdk::Drag>&, Gdk::DragCancelReason) {
        say("Not attached -- drop it on the mail you are writing.");
        m_dropped = true;   // said; drag_end stays quiet
        return false;
    }, false);
    drag->signal_drag_end().connect([this](const Glib::RefPtr<Gdk::Drag>&, bool) {
        if (!m_dropped)
            say("Dropped " + core::glance_ics_name(m_glance) +
                " -- it goes with the mail. On the iPhone, open it in Mail and tap Add All.");
    });
    m_chip.add_controller(drag);

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) {
        const std::string path = write_drag_file();
        if (path.empty()) return;
        auto launcher = Gtk::FileLauncher::create(Gio::File::create_for_path(path));
        launcher->open_containing_folder(*this, [launcher](Glib::RefPtr<Gio::AsyncResult>& r) {
            try { launcher->open_containing_folder_finish(r); } catch (const Glib::Error&) {}
        });
    });
    m_chip.add_controller(click);

    m_status.set_xalign(0);
    m_status.set_wrap(true);
    m_status.add_css_class("dim-label");
    m_status.set_text("Copy for Keep or Notes · Email, then drag the .ics in · Save .ics keeps a copy");
    foot->append(m_status);
    page->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
    page->append(*foot);
    set_child(*page);

    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint key, guint, Gdk::ModifierType) {
            if (key != GDK_KEY_Escape) return false;
            close();
            return true;
        }, false);
    add_controller(keys);
}

void GlanceWindow::set_source(const core::NodeSource* src, const core::TaskIndex* tasks) {
    m_src = src;
    m_tasks = tasks;
    m_last_sig.clear();
    refresh();
}

void GlanceWindow::set_mail_to(const std::string& to) {
    if (std::string(m_to.get_text()) != to) m_to.set_text(to);
}

void GlanceWindow::show(Gtk::Window& parent) {
    set_transient_for(parent);
    present();
    m_last_sig.clear();
    refresh();
    m_copy.grab_focus();
}

void GlanceWindow::refresh() {
    if (!get_visible()) return;
    if (!m_src || !m_tasks) {
        m_glance = core::Glance{};
        m_glance.day = core::day_start(now_s());
        m_glance.title = "jot";
        m_glance.summary = "No jots folder open.";
    } else {
        m_glance = core::glance(*m_src, *m_tasks, now_s());
    }
    // Redraw only when the words changed: the minute tick mostly changes none.
    const std::string sig = m_glance.title + "\n" + core::glance_text(m_glance);
    std::string ids;
    for (const auto& s : m_glance.sections)
        for (const auto& it : s.items) ids += it.id + ",";
    if (sig + ids == m_last_sig) return;
    m_last_sig = sig + ids;
    fill();
    log_info(m_glance.summary + " -- " + std::to_string(m_glance.sections.size()) + " section(s)");
}

void GlanceWindow::fill() {
    m_day.set_text(long_day(m_glance.day));
    m_summary.set_text(m_glance.summary);
    m_chip_name.set_text(core::glance_ics_name(m_glance));
    while (auto* c = m_column.get_first_child()) m_column.remove(*c);

    if (m_glance.sections.empty()) {
        auto* none = Gtk::make_managed<widgets::Label>(widgets::unregistered, "glance.empty",
            m_src ? "Nothing late, due, starting or flagged today. A clear day."
                  : "Open a jots folder to see its day.");
        none->add_css_class("dim-label");
        none->set_wrap(true);
        none->set_margin_top(24);
        m_column.append(*none);
        return;
    }

    bool errands_head = false;
    for (const auto& s : m_glance.sections) {
        auto add_head = [&](const std::string& words, const char* cls) {
            auto* h = Gtk::make_managed<widgets::Label>(widgets::unregistered, "glance.head", words);
            h->set_xalign(0);
            h->add_css_class("jot-glance-head");
            h->add_css_class(cls);
            h->set_margin_start(8);
            h->set_margin_top(14);
            h->set_margin_bottom(2);
            m_column.append(*h);
        };
        if (s.kind == core::GlanceKind::Errands) {
            if (!errands_head) { add_head("ERRANDS", kind_class(s.kind)); errands_head = true; }
            auto* place = Gtk::make_managed<widgets::Label>(widgets::unregistered, "glance.place", s.head);
            place->set_xalign(0);
            place->add_css_class("jot-glance-place");
            place->set_margin_start(8);
            place->set_margin_top(4);
            m_column.append(*place);
        } else {
            std::string h = s.head;
            for (auto& c : h) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            add_head(h, kind_class(s.kind));
        }
        for (const auto& it : s.items) {
            auto* b = Gtk::make_managed<widgets::Button>(widgets::unregistered, "glance.row." + it.id);
            b->add_css_class("flat");
            b->add_css_class("jot-glance-row");
            auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
            auto* dot = Gtk::make_managed<Gtk::Label>("●");
            dot->add_css_class("jot-glance-dot");
            dot->add_css_class(kind_class(s.kind));
            dot->set_valign(Gtk::Align::START);
            box->append(*dot);
            auto* words = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 1);
            auto* t = Gtk::make_managed<Gtk::Label>(it.title);
            t->set_xalign(0);
            t->set_wrap(true);
            t->add_css_class("jot-glance-title");
            words->append(*t);
            if (!it.detail.empty()) {
                auto* d = Gtk::make_managed<Gtk::Label>(it.detail);
                d->set_xalign(0);
                d->set_wrap(true);
                d->add_css_class("dim-label");
                d->add_css_class("caption");
                words->append(*d);
            }
            words->set_hexpand(true);
            box->append(*words);
            b->set_child(*box);
            b->set_tooltip_text("Show this note");
            const core::NodeId id = it.id;
            b->signal_clicked().connect([this, id]() { m_sig_goto.emit(id); });
            m_column.append(*b);
        }
    }
}

std::string GlanceWindow::write_drag_file() {
    refresh();
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::path(Glib::get_user_cache_dir()) / "jot" / "glance";
    fs::create_directories(dir, ec);
    // Only the day's file lives here: older days are swept as a new one is written.
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".ics" && e.path().filename() != core::glance_ics_name(m_glance))
            fs::remove(e.path(), ec);
    const fs::path path = dir / core::glance_ics_name(m_glance);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << core::glance_ics(m_glance, now_s());
    f.close();
    if (!f) {
        say("Could not write " + path.string());
        return {};
    }
    return path.string();
}

void GlanceWindow::say(const std::string& text) {
    m_status.set_text(text);
    log_info(text);
}

void GlanceWindow::on_copy() {
    refresh();
    const std::string text = core::glance_text(m_glance);
    const std::string html = core::glance_html(m_glance);
    // Rich first, so an editor that takes html takes it; the string value
    // last, for every text field (Keep, an entry, a terminal).
    Glib::Value<Glib::ustring> v;
    v.init(Glib::Value<Glib::ustring>::value_type());
    v.set(text);
    std::vector<Glib::RefPtr<Gdk::ContentProvider>> flavours{
        Gdk::ContentProvider::create("text/html", Glib::Bytes::create(html.data(), html.size())),
        Gdk::ContentProvider::create("text/plain;charset=utf-8",
                                     Glib::Bytes::create(text.data(), text.size())),
        Gdk::ContentProvider::create(v)};
    get_clipboard()->set_content(Gdk::ContentProvider::create(flavours));
    const int n = m_glance.count();
    say("Copied the Glance (" + (n == 1 ? std::string("1 thing") : std::to_string(n) + " things") +
        ") -- paste it into Keep, Notes or a message.");
}

void GlanceWindow::on_email() {
    refresh();
    const std::string uri = core::glance_mailto(m_glance, m_to.get_text());
    auto launcher = Gtk::UriLauncher::create(uri);
    launcher->launch(*this, [this, launcher](Glib::RefPtr<Gio::AsyncResult>& r) {
        try {
            launcher->launch_finish(r);
            say(m_to.get_text().empty() ? "Opened a new mail -- add your address and send."
                                        : "Opened a new mail to " + std::string(m_to.get_text()) +
                                              " -- press Send.");
        } catch (const Glib::Error& e) {
            if (e.matches(GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) return;
            say("No mail app answered (" + std::string(e.what()) +
                "). Copy works anywhere -- paste it into a new mail.");
        }
    });
}

void GlanceWindow::on_save() {
    refresh();
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Save the day as a calendar file");
    dialog->set_initial_name(core::glance_ics_name(m_glance));
    std::string start = m_save_dir;
    std::error_code ec;
    if (start.empty() || !std::filesystem::is_directory(start, ec))
        start = Glib::get_user_special_dir(Glib::UserDirectory::DOWNLOAD);
    if (start.empty() || !std::filesystem::is_directory(start, ec)) start = Glib::get_home_dir();
    if (!start.empty() && std::filesystem::is_directory(start, ec))
        dialog->set_initial_folder(Gio::File::create_for_path(start));
    dialog->save(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto file = dialog->save_finish(result);
            if (!file) return;
            const std::string path = file->get_path();
            const std::string ics = core::glance_ics(m_glance, now_s());
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f << ics;
            f.close();
            if (!f) {
                say("Could not write " + path);
                return;
            }
            m_save_dir = std::filesystem::path(path).parent_path().string();
            m_sig_prefs.emit(m_to.get_text(), m_save_dir);
            say("Saved " + std::filesystem::path(path).filename().string() +
                " -- Google Calendar › Settings › Import takes it, or attach it to a mail to your phone.");
        } catch (const Glib::Error& e) {
            log_info(std::string("save dismissed (") + e.what() + ")");
        }
    });
}

}  // namespace jot
