// Shell_backup.cpp -- s075. Backups: a dated rsync snapshot of the open jots
// folder each day, refreshed as you work and on quit; Restore copies a day out
// as a NEW folder.
//
// (Scott, Oct 8: "a backup process that can be automatic"; "rsync is the linux
// way"; "a pref for where the backups go and the default is in the standard
// dot app folder".)
//
// core/Backup makes every decision (when, where, which days go, the command);
// this file only spawns it and reports. rsync runs as a child process, never
// on the main loop -- a big attachments folder must not freeze the window --
// and on quit it is left running on its own: it outlives jot by design.

#include "Shell.hpp"
#include "PreferencesWindow.hpp"
#include "Log.hpp"
#include "core/Backup.hpp"
#include "core/Project.hpp"
#include "core/Prefs.hpp"

#include <giomm/file.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <glibmm/spawn.h>
#include <gtkmm/alertdialog.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filelauncher.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>

#include <ctime>
#include <filesystem>
#include <fstream>

#include <sys/wait.h>

namespace fs = std::filesystem;

namespace jot {

namespace {

constexpr long kGap = 3600;   // today's snapshot is refreshed at most hourly

std::string tilde(const std::string& p) {
    const std::string home = Glib::get_home_dir();
    if (!home.empty() && p.rfind(home, 0) == 0) return "~" + p.substr(home.size());
    return p;
}

std::string first_line_of(const fs::path& f) {
    std::ifstream in(f);
    std::string line, last;
    while (std::getline(in, line))
        if (!line.empty()) { last = line; break; }
    return last;
}

}  // namespace

std::string Shell::backup_root_now() const {  // helper: the root in force
    return core::backup_root(Glib::get_user_data_dir(), m_prefs.backup_dir);
}

// Where this folder's days live, without creating anything: "" when the root
// is not there (a drive not plugged in) -- the reader treats that as "none".
std::string Shell::backup_slot_for(const std::string& dir) const {  // helper
    const std::string root = backup_root_now();
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return "";
    const fs::path plain = fs::path(root) / fs::path(dir).filename();
    const std::string taken = first_line_of(plain / ".source");
    return (fs::path(root) / core::backup_slot_name(dir, taken)).string();
}

// ─────────────────────────────────────────────────────────────────────────────
// backup_run -- one rsync for a jots folder, if core says it is due.
//
// `force` is Back Up Now: run even if nothing changed. `detach` is quit: spawn
// and let it go -- jot is about to exit and the child carries on alone.
// Returns true if an rsync was started.
// ─────────────────────────────────────────────────────────────────────────────
bool Shell::backup_run(const std::string& dir, bool force, bool detach) {  // helper
    auto lg = log::get(log::Area::Io);
    if (dir.empty()) return false;
    if (m_backup_pid != 0) {
        if (lg) lg->info("backup: one is already running -- skipped");
        return false;
    }
    const std::string rsync = Glib::find_program_in_path("rsync");
    if (rsync.empty()) {
        m_backup_problem = "rsync is not installed, so there are no backups. "
                           "Install it (sudo dnf install rsync) and they start on their own.";
        if (lg) lg->warn("backup: rsync not found on PATH");
        refresh_backup_prefs();
        return false;
    }
    const std::string root = backup_root_now();
    std::error_code ec;
    if (!m_prefs.backup_dir.empty() && !fs::is_directory(root, ec)) {
        // A chosen place that is not there is a drive not plugged in, not a
        // folder to make: making it would put the backups on the wrong disk.
        m_backup_problem = "Skipped — " + tilde(root) + " is not there (a drive not plugged in?). "
                           "jot tries again later.";
        if (lg) lg->warn("backup: '{}' is not there -- skipped", root);
        refresh_backup_prefs();
        return false;
    }
    if (core::path_inside(root, dir)) {
        m_backup_problem = "The backup folder is inside the jots folder — choose another place.";
        if (lg) lg->warn("backup: root '{}' is inside '{}' -- refused", root, dir);
        refresh_backup_prefs();
        return false;
    }
    fs::create_directories(root, ec);
    const fs::path plain = fs::path(root) / fs::path(dir).filename();
    const std::string taken = first_line_of(plain / ".source");
    const fs::path slot = fs::path(root) / core::backup_slot_name(dir, taken);
    fs::create_directories(slot, ec);
    if (ec) {
        m_backup_problem = "Could not make " + tilde(slot.string()) + ": " + ec.message();
        if (lg) lg->error("backup: {}", m_backup_problem);
        refresh_backup_prefs();
        return false;
    }
    if (!fs::exists(slot / ".source")) std::ofstream(slot / ".source") << dir << "\n";

    const std::time_t now = std::time(nullptr);
    const std::time_t last = core::read_last_ok(slot.string());
    const std::time_t newest = core::newest_change(dir);
    if (core::backup_need(last, newest, now, kGap, force) == core::BackupNeed::None) return false;

    const std::string today = core::backup_day_of(now);
    auto days = core::backup_days(slot.string());
    const std::string link = core::backup_link_day(days, today);
    days.push_back(today);
    for (const auto& d : core::backup_prune(days)) {
        fs::remove_all(slot / d, ec);
        if (lg) lg->info("backup: pruned {}", (slot / d).string());
    }

    const auto argv = core::backup_command(rsync, dir, (slot / today).string(),
                                           link.empty() ? "" : (slot / link).string(),
                                           (slot / "last-ok").string());
    try {
        if (detach) {
            Glib::spawn_async("/", argv, Glib::SpawnFlags::DEFAULT);
            if (lg) lg->info("backup: {} -> {} (left running as jot quits)", dir, (slot / today).string());
            return true;
        }
        Glib::Pid pid = 0;
        Glib::spawn_async("/", argv, Glib::SpawnFlags::DO_NOT_REAP_CHILD, Glib::SlotSpawnChildSetup(), &pid);
        m_backup_pid = static_cast<int>(pid);
        if (lg) lg->info("backup: {} -> {}{}", dir, (slot / today).string(),
                         link.empty() ? "" : " (unchanged files linked to " + link + ")");
        const std::string slot_s = slot.string();
        Glib::signal_child_watch().connect(
            [this, slot_s](Glib::Pid p, int status) {
                Glib::spawn_close_pid(p);
                m_backup_pid = 0;
                const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                if (ok) {
                    m_backup_problem.clear();
                } else {
                    const std::string why = first_line_of(fs::path(slot_s) / "last-ok.err");
                    m_backup_problem = "The last backup failed" + (why.empty() ? std::string(".") : ": " + why);
                }
                if (auto l = log::get(log::Area::Io))
                    l->info("backup: {}", ok ? "done" : m_backup_problem);
                refresh_backup_prefs();
            },
            pid);
    } catch (const Glib::Error& e) {
        m_backup_problem = std::string("Could not start rsync: ") + e.what();
        if (lg) lg->error("backup: {}", m_backup_problem);
        refresh_backup_prefs();
        return false;
    }
    refresh_backup_prefs();
    return true;
}

void Shell::backup_auto() {  // helper: the clock / open / close check
    if (!m_prefs.backups_on || !m_project) return;
    if (m_project->dirty()) m_project->flush();
    backup_run(m_project->dir(), false, false);
}

void Shell::refresh_backup_prefs() {  // helper: the Preferences rows say what is true
    if (!m_preferences) return;
    const std::string root = backup_root_now();
    std::string says;
    if (!m_project) {
        says = "Open a jots folder to back it up.";
    } else if (m_backup_pid != 0) {
        says = "Backing up…";
    } else {
        const std::string slot = backup_slot_for(m_project->dir());
        const std::time_t last = slot.empty() ? 0 : core::read_last_ok(slot);
        says = core::backup_when_text(last, std::time(nullptr));
        const auto n = slot.empty() ? 0 : core::backup_days(slot).size();
        if (n > 0) says += " · " + std::to_string(n) + (n == 1 ? " day kept" : " days kept");
        if (!m_prefs.backups_on) says += " · automatic backups are off";
    }
    if (!m_backup_problem.empty()) says += "\n" + m_backup_problem;
    m_preferences->set_backup(m_prefs.backups_on, tilde(root), m_prefs.backup_dir.empty(), says,
                              m_backup_pid != 0, m_project != nullptr);
}

void Shell::on_backup_now() {  // handler: Back Up Now
    if (!m_project) return;
    m_project->flush();
    if (!backup_run(m_project->dir(), true, false) && m_backup_problem.empty() && m_backup_pid != 0)
        if (auto lg = log::get(log::Area::Io)) lg->info("backup now: one is already running");
}

void Shell::on_backup_choose() {  // handler: Preferences > Backups > Choose...
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Keep jot's backups in…");
    dialog->set_accept_label("Keep Them Here");
    std::error_code ec;
    if (fs::is_directory(backup_root_now(), ec))
        dialog->set_initial_folder(Gio::File::create_for_path(backup_root_now()));
    Gtk::Window& parent = m_preferences ? static_cast<Gtk::Window&>(*m_preferences)
                                        : static_cast<Gtk::Window&>(*this);
    dialog->select_folder(parent, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& r) {
        Glib::RefPtr<Gio::File> f;
        try { f = dialog->select_folder_finish(r); } catch (const Glib::Error&) { return; }
        if (!f || f->get_path().empty()) return;
        const std::string path = f->get_path();
        if (m_project && core::path_inside(path, m_project->dir())) {
            m_backup_problem = "That folder is inside the jots folder — choose another place.";
            refresh_backup_prefs();
            return;
        }
        m_prefs.backup_dir = path;
        core::save_prefs(m_prefs_file, m_prefs);
        m_backup_problem.clear();
        if (auto lg = log::get(log::Area::Io)) lg->info("backups now go to '{}'", path);
        refresh_backup_prefs();
        backup_auto();   // the first day there, now
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Restore -- the days, newest first; pick one; it is copied out as a NEW
// folder beside the open one ("home (restored 8 Oct 2026).jots"). Never over
// the folder you have open: that one may be the only copy of today.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_backup_restore() {  // handler: Restore from Backup...
    if (!m_project) return;
    const std::string slot = backup_slot_for(m_project->dir());
    const auto days = slot.empty() ? std::vector<std::string>{} : core::backup_days(slot);
    if (days.empty()) {
        auto alert = Gtk::AlertDialog::create("No backups of this jots folder yet");
        alert->set_detail(slot.empty() && !m_prefs.backup_dir.empty()
                              ? "The backup folder " + tilde(backup_root_now()) + " is not there "
                                "— is its drive plugged in?"
                              : "The first one is made a few seconds after a jots folder opens, "
                                "or with Back Up Now.");
        alert->show(*this);
        return;
    }

    m_restore_win = std::make_unique<Gtk::Window>();
    auto* win = m_restore_win.get();
    win->set_name("shell.restore");
    win->set_title("Restore from Backup");
    win->set_transient_for(*this);
    win->set_modal(true);
    win->set_default_size(420, 420);
    win->set_hide_on_close(true);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(16);
    auto* head = Gtk::make_managed<Gtk::Label>(
        "Bring back " + fs::path(m_project->dir()).filename().string() +
        " as it was on a day. It comes back as a new folder beside this one — "
        "nothing you have now is changed.");
    head->set_wrap(true);
    head->set_xalign(0.0f);
    box->append(*head);

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    auto* list = Gtk::make_managed<Gtk::ListBox>();
    list->set_name("restore.days");
    list->set_selection_mode(Gtk::SelectionMode::SINGLE);
    const std::string today = core::backup_day_of(std::time(nullptr));
    const std::string yesterday = core::backup_day_of(std::time(nullptr) - 86400);
    for (auto it = days.rbegin(); it != days.rend(); ++it) {
        std::string text = core::backup_day_label(*it);
        if (*it == today) text += "  ·  today (as of the last backup)";
        else if (*it == yesterday) text += "  ·  yesterday";
        auto* row = Gtk::make_managed<Gtk::Label>(text);
        row->set_xalign(0.0f);
        row->set_margin(6);
        row->set_name(*it);   // the day, read back on Restore
        list->append(*row);
    }
    if (auto* first = list->get_row_at_index(0)) list->select_row(*first);
    scroll->set_child(*list);
    box->append(*scroll);

    auto* buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    buttons->set_halign(Gtk::Align::END);
    auto* show = Gtk::make_managed<Gtk::Button>("Show in Files");
    show->set_tooltip_text("Open the backups folder -- each day is a plain folder");
    show->signal_clicked().connect([this, slot]() {
        auto launcher = Gtk::FileLauncher::create(Gio::File::create_for_path(slot));
        launcher->launch(*this, [](Glib::RefPtr<Gio::AsyncResult>&) {});
    });
    auto* cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel->signal_clicked().connect([win]() { win->close(); });
    auto* go = Gtk::make_managed<Gtk::Button>("Restore");
    go->add_css_class("suggested-action");
    go->signal_clicked().connect([this, win, list, slot]() {
        auto* row = list->get_selected_row();
        if (!row || !row->get_child()) return;
        const std::string day = row->get_child()->get_name();
        win->close();
        restore_day(slot, day);
    });
    list->signal_row_activated().connect([go](Gtk::ListBoxRow*) { go->activate(); });
    buttons->append(*show);
    buttons->append(*cancel);
    buttons->append(*go);
    box->append(*buttons);
    win->set_child(*box);
    win->present();
}

void Shell::restore_day(const std::string& slot, const std::string& day) {  // helper
    if (!m_project) return;
    const std::string dir = m_project->dir();
    const fs::path parent = fs::path(dir).parent_path();
    const std::string name = core::backup_restore_name(dir, day);
    fs::path dest = parent / name;
    for (int i = 2; fs::exists(dest) && i < 100; ++i) {
        std::string n = name;
        n.insert(n.size() - 5, " " + std::to_string(i));   // before ".jots"
        dest = parent / n;
    }
    std::error_code ec;
    fs::copy(fs::path(slot) / day, dest, fs::copy_options::recursive, ec);
    auto lg = log::get(log::Area::Io);
    if (ec) {
        if (lg) lg->error("restore: {} -> {}: {}", day, dest.string(), ec.message());
        auto alert = Gtk::AlertDialog::create("Could not restore");
        alert->set_detail(tilde(dest.string()) + ": " + ec.message());
        alert->show(*this);
        return;
    }
    if (lg) lg->info("restore: {} -> {}", (fs::path(slot) / day).string(), dest.string());
    auto alert = Gtk::AlertDialog::create("Restored as " + dest.filename().string());
    alert->set_detail("In " + tilde(parent.string()) + ". The folder you have open is unchanged. "
                      "Open the restored one to look through it, or copy a note back from it.");
    alert->set_buttons({"Close", "Open It"});
    alert->set_cancel_button(0);
    alert->set_default_button(1);
    const std::string target = dest.string();
    alert->choose(*this, [this, alert, target](Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            if (alert->choose_finish(res) == 1) open_jots(target);
        } catch (const Glib::Error&) {}
    });
}

}  // namespace jot
