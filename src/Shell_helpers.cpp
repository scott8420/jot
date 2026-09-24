#include "Shell.hpp"
#include "DrawerPane.hpp"
#include "EditorPane.hpp"
#include "JotsFolderDialog.hpp"
#include "TreePane.hpp"
#include "TodayPane.hpp"
#include "Log.hpp"
#include "core/Prefs.hpp"
#include "core/Notify.hpp"
#include "core/Projection.hpp"
#include "core/Pending.hpp"
#include "core/Recents.hpp"

#include <giomm/notification.h>
#include <giomm/themedicon.h>
#include <glibmm/main.h>

#include <spdlog/fmt/fmt.h>

#include <ctime>


#include <cstdlib>

#include <gtkmm/alertdialog.h>

#include <giomm/menuitem.h>
#include <glibmm/miscutils.h>
#include <glibmm/variant.h>

#include <filesystem>

// Shell_helpers.cpp -- HELPERS. Reusable orchestration called from zones,
// bindings, and handlers: the recents seam (path resolution, persist, repaint)
// and the selection-sensitive action state.

namespace jot {

std::string Shell::recents_file() const {  // helper: XDG path (UI-side resolution)
    // The UI resolves the XDG dir (Glib here) and hands the core a plain path --
    // the core stays GTK-free (docs/two-layer-seam.md).
    return (std::filesystem::path(Glib::get_user_data_dir()) / "jot" / "recent.json")
        .string();
}

void Shell::note_recent(const std::string& path) {  // helper: push + persist + repaint
    if (path.empty()) return;
    core::recents_add(m_recents, path);
    core::save_recents(m_recents_file, m_recents);
    rebuild_recents_menu();
}

void Shell::rebuild_recents_menu() {  // helper: repaint the submenu
    if (!m_recents_menu) return;
    m_recents_menu->remove_all();

    // Display name = folder basename; identical basenames get their parent dir
    // appended so they're distinguishable (Curvz's disambiguation).
    std::vector<std::string> names(m_recents.size());
    for (std::size_t i = 0; i < m_recents.size(); ++i)
        names[i] = std::filesystem::path(m_recents[i]).filename().string();
    for (std::size_t i = 0; i < names.size(); ++i)
        for (std::size_t j = i + 1; j < names.size(); ++j)
            if (names[i] == names[j]) {
                const std::string pi =
                    std::filesystem::path(m_recents[i]).parent_path().filename().string();
                const std::string pj =
                    std::filesystem::path(m_recents[j]).parent_path().filename().string();
                if (!pi.empty()) names[i] += "  (" + pi + ")";
                if (!pj.empty()) names[j] += "  (" + pj + ")";
            }

    for (std::size_t i = 0; i < m_recents.size(); ++i) {
        auto item = Gio::MenuItem::create(names[i], "");
        item->set_action_and_target(
            "recents.open", Glib::Variant<Glib::ustring>::create(m_recents[i]));
        m_recents_menu->append_item(item);
    }

    auto clear_section = Gio::Menu::create();
    clear_section->append("Clear recent", "recents.clear");
    m_recents_menu->append_section("", clear_section);
    if (m_recents_clear) m_recents_clear->set_enabled(!m_recents.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Where a NEW jots folder is offered. Not a default jots folder -- there is no
// such thing any more, which is the whole of the s005 correction. This is the
// folder the naming dialog opens in, and the user changes it in one click.
//
// ~/Documents, because notes are documents. The XDG special dir rather than a
// hardcoded "Documents" so a localised or relocated one is honoured, with the
// home directory as the fallback -- never the data dir, which is where s003
// put notes and where nobody would look for them.
// ─────────────────────────────────────────────────────────────────────────────
std::string Shell::default_jots_location() const {  // helper: where a new folder is offered
    const std::string docs = Glib::get_user_special_dir(Glib::UserDirectory::DOCUMENTS);
    return docs.empty() ? Glib::get_home_dir() : docs;
}

// Display form of a path: ~ for the home dir. Display only -- nothing parses
// it back, and every path that reaches the disk is the absolute one.
static std::string tilde_path(const std::string& path) {
    const std::string home = Glib::get_home_dir();
    if (!home.empty() && path.rfind(home + "/", 0) == 0)
        return "~" + path.substr(home.size());
    if (path == home) return "~";
    return path;
}

// ─────────────────────────────────────────────────────────────────────────────
// update_jots_title -- the header says where you are.
//
// Fault 2 of the three s005 exists to fix: the path was nowhere in the UI. It
// is now in three places that cannot disagree, because all three are written
// here from one source -- the two header lines, the menu's section label, and
// the button's tooltip. The window title keeps the folder name too, for the
// taskbar and alt-tab, where a custom title widget is not visible.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::update_jots_title() {  // helper: repaint the header title + its menu
    const std::string dir = m_project ? m_project->dir() : std::string{};
    const bool have = !dir.empty();

    if (have) {
        // The NAME, not the folder name: `Field notes.jots` reads as "Field
        // notes". The suffix is a disk convention and the header is not disk.
        const std::string name = core::jots_display_name(dir);
        m_jots_name.set_text(name);
        m_jots_where.set_text(tilde_path(std::filesystem::path(dir).parent_path().string()));
        m_jots_button.set_tooltip_text(dir);
        set_title("jot \u2014 " + name);
    } else if (m_stress) {
        // The D6 instrument. Say what it is, loudly, because a synthetic set
        // that looked like real notes would be a genuinely alarming thing to
        // find yourself typing into.
        m_jots_name.set_text("Stress test");
        m_jots_where.set_text("synthetic \u2014 nothing is saved");
        m_jots_button.set_tooltip_text("JOT_STRESS is set; this set is generated and discarded");
        set_title("jot \u2014 stress test");
    } else {
        // The scratch buffer. "Unsaved jots" is the honest label: there ARE
        // jots, they are just nowhere yet, and "no jots folder" would read as
        // "nothing here" over a tree with notes in it.
        m_jots_name.set_text("Unsaved jots");
        m_jots_where.set_text("not saved to disk yet");
        m_jots_button.set_tooltip_text("");
        set_title("jot \u2014 unsaved");
    }

    if (m_act_open_in_files) m_act_open_in_files->set_enabled(have);
    if (m_act_copy_path)     m_act_copy_path->set_enabled(have);
    if (m_act_relocate)      m_act_relocate->set_enabled(have);
    if (m_act_rename)        m_act_rename->set_enabled(have);

    if (!m_jots_menu) return;
    m_jots_menu->remove_all();
    auto here = Gio::Menu::create();
    here->append("Open in Files", "win.open-in-files");
    here->append("Copy path", "win.copy-path");
    // The section LABEL is the absolute path. A menu item would make the path
    // look clickable and it isn't; a section heading is GTK4's way of saying
    // "this is what the following verbs are about."
    m_jots_menu->append_section(have ? dir : "No jots folder open", here);

    auto move = Gio::Menu::create();
    move->append("Rename jots\u2026", "win.rename-jots");
    move->append("Relocate jots\u2026", "win.relocate-jots");
    m_jots_menu->append_section(move);
}

// ─────────────────────────────────────────────────────────────────────────────
// The naming dialog, opened. One instance at a time, rebuilt per occasion
// because the mode is fixed at construction and there are only three.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::name_jots(JotsFolderDialog::Mode mode, const std::string& location,
                      const std::string& name, JotsFolderDialog::Done done) {
    m_name_dialog = std::make_unique<JotsFolderDialog>(*this, mode, location, name,
                                                       std::move(done));
    m_name_dialog->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// The scratch buffer.
//
// jot runs perfectly well with no jots folder: the store is a MemoryNodes and
// the app is a pad you can type into immediately. That is the scribble pad's
// front door on a fresh machine -- capture first, file second -- and it is the
// shape s005's first draft got backwards by asking where notes should live
// BEFORE there were any.
//
// The filing question arrives at the moment it becomes a real question: when
// you close, holding something worth keeping.
// ─────────────────────────────────────────────────────────────────────────────

// "Has anything been written?" -- deliberately not "is the store non-empty".
// build_ui mints one blank note so there is something to type into, and a
// single untitled empty note is the app as it opened, not work. Anything more
// than that counts.
bool Shell::scratch_has_content() const {
    if (m_project || m_stress || !m_store) return false;
    if (m_store->count() > 1) return true;
    for (const auto& id : m_store->children("")) {
        const core::Node* n = m_store->find(id);
        if (n && (!n->title.empty() || !n->body.empty())) return true;
    }
    return false;
}

// Write everything in the current store into `target` and switch to it.
//
// Two callers, one operation: the close prompt's Save arm (from the scratch
// buffer) and Save as... (from either). core::Project::adopt does the
// load-bearing half -- re-mint every id, remap every parent through the same
// map, preserve order -- and is selftested, because a parent mapped to a stale
// id is a subtree that silently goes missing. What is here is only the swap,
// which is the same swap open_jots does.
void Shell::save_scratch(const std::string& target) {
    auto jots = std::make_unique<core::Project>();
    if (!jots->open(target)) {
        report_problem("Could not create that folder",
                       "jot could not open " + target + " to write into.");
        return;
    }

    const std::size_t had = m_store ? m_store->count() : 0;
    const std::size_t took = jots->adopt(*m_store);
    if (took != had) {
        // adopt walks from the roots, so a shortfall means the scratch buffer
        // held something the tree could not reach. It has never happened and
        // it must not pass silently if it does.
        if (auto lg = log::get(log::Area::Model))
            lg->error("save: adopted {} of {} node(s) -- something was unreachable",
                      took, had);
    }
    if (auto lg = log::get(log::Area::Io))
        lg->info("wrote {} note(s) into '{}'", took, target);

    m_project = jots.get();
    m_store = std::move(jots);
    m_store->on_changed(sigc::mem_fun(*this, &Shell::on_model_changed));
    m_tree->set_source(m_store.get());
    m_editor->set_source(m_store.get());
    m_links.rebuild(*m_store);
    m_drawer->set_source(m_store.get(), &m_links);
    m_drawer->set_jots_dir(m_project ? m_project->dir() : std::string{});
    note_recent(target);
    update_jots_title();

    m_tree->rebuild();
    auto roots = m_store->children("");
    if (!roots.empty()) { m_tree->select(roots.front()); on_selection_changed(roots.front()); }
    update_note_actions();
    drain_pending();   // the scratch buffer just became a folder; the spool can land
}

// ─────────────────────────────────────────────────────────────────────────────
// guard_scratch -- do `then`, but not at the cost of unsaved notes.
//
// Every way of leaving the scratch buffer routes through here: closing the
// window, opening another jots folder, picking one from recents. Without it
// each of those is a separate chance to drop the user's work, and they would
// be found one at a time, in use, by losing notes.
//
// Discard is unrecoverable -- there is no file to fall back to -- so Save is
// the default and Discard is not. No confirm-your-confirm; a second dialog
// guarding the first is worse than the risk it guards.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::guard_scratch(std::function<void()> then) {
    if (!scratch_has_content()) { then(); return; }

    auto alert = Gtk::AlertDialog::create();
    alert->set_message("Save these notes?");
    alert->set_detail("They have not been written to disk. Discarding them cannot be undone.");
    alert->set_buttons({"Cancel", "Discard", "Save"});
    alert->set_cancel_button(0);
    alert->set_default_button(2);
    alert->set_modal(true);
    alert->choose(*this,
        [this, alert, then](const Glib::RefPtr<Gio::AsyncResult>& result) {
            int button = 0;
            try {
                button = alert->choose_finish(result);
            } catch (const Glib::Error&) {
                return;   // dismissed == Cancel
            }
            if (button == 1) {                       // Discard
                if (auto lg = log::get(log::Area::Model)) lg->warn("unsaved notes discarded");
                then();
            } else if (button == 2) {                // Save
                name_jots(JotsFolderDialog::Mode::Save, default_jots_location(), "",
                          [this, then](const std::string& target) {
                              save_scratch(target);
                              // Only continue if the save actually took. A
                              // failed save that still closed the window would
                              // be the worst outcome this function can have.
                              if (m_project) then();
                          });
            }
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// relocate_to -- move the whole jots folder.
//
// Safe, and safe SPECIFICALLY because of the shape D1 chose: nothing inside
// holds an absolute path, jot.json addresses by id, and filenames are uuids.
// So this is a rename of one directory and a reopen -- the property that made
// a drag cheap pays again somewhere nobody was looking.
//
// The ordering matters more than the move. Flush FIRST, so the deferred body
// write lands while its folder still exists; only then touch the disk; only
// then reopen. Nothing holds an open file handle between calls, so there is no
// close step -- if there ever is, it goes here, before relocate_jots.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::relocate_to(const std::string& target) {  // helper: flush, move, reopen there
    if (!m_project) return;
    const std::string from = m_project->dir();
    if (from == target) return;

    m_project->flush();
    if (const std::string err = core::relocate_jots(from, target); !err.empty()) {
        if (auto lg = log::get(log::Area::Io))
            lg->error("relocate '{}' -> '{}': {}", from, target, err);
        report_problem("Could not move your jots", err + "\n\nNothing was moved.");
        return;
    }
    if (auto lg = log::get(log::Area::Io)) lg->info("relocated '{}' -> '{}'", from, target);

    core::recents_remove(m_recents, from);
    core::save_recents(m_recents_file, m_recents);
    open_jots(target);
}

// A failure the user asked for is a failure the user has to be told about. The
// log is the trace channel; this is the other one (CANON: convergent evidence
// -- a silent failure only ever reaches one of them).
void Shell::report_problem(const std::string& summary, const std::string& detail) {
    if (auto lg = log::get(log::Area::Shell)) lg->warn("{}: {}", summary, detail);
    auto alert = Gtk::AlertDialog::create();
    alert->set_message(summary);
    alert->set_detail(detail);
    alert->set_modal(true);
    alert->show(*this);
}

// THE SWAP POINT, and the whole test s002 was built to run. One of these two
// branches constructs a NodeSource; everything above the seam -- TreePane,
// EditorPane, every handler -- sees only the interface and does not know which
// ran. JOT_STRESS keeps the D6 instrument reachable without a jots folder, and
// deliberately does NOT touch the disk.
void Shell::open_store() {  // helper: construct the NodeSource
    if (const char* stress = std::getenv("JOT_STRESS"); stress && *stress) {
        if (const int n = std::atoi(stress); n > 0) {
            if (auto lg = log::get(log::Area::Model))
                lg->warn("JOT_STRESS={} -- in-memory synthetic set, nothing is saved", n);
            auto mem = std::make_unique<core::MemoryNodes>();
            mem->reset(core::stress_nodes(n));
            m_project = nullptr;
            m_stress = true;
            m_store = std::move(mem);
            return;
        }
    }

    // NO JOTS FOLDER -- the scratch buffer. No recents means we have never been
    // told where notes go, and s005's correction is that we do not decide that
    // silently AND do not make it a toll gate either. The store is an in-memory
    // one and TOUCHES NO DISK; the app is a pad you can type into at once, and
    // guard_scratch asks where it should live on the way out.
    if (m_recents.empty()) {
        if (auto lg = log::get(log::Area::Io))
            lg->info("no jots folder -- running on a scratch buffer, nothing on disk yet");
        m_project = nullptr;
        m_store = std::make_unique<core::MemoryNodes>();
        return;
    }

    const std::string dir = m_recents.front();
    auto jots = std::make_unique<core::Project>();
    if (!jots->open(dir)) {
        // A jots folder we cannot open is not a reason to come up with no surfaces.
        if (auto lg = log::get(log::Area::Io)) lg->error("jots folder '{}': cannot open", dir);
        m_project = nullptr;
        m_store = std::make_unique<core::MemoryNodes>();
        return;
    }
    if (auto lg = log::get(log::Area::Io))
        lg->info("jots folder '{}': {} node(s){}", dir, jots->count(),
                 jots->recovered_orphans()
                     ? fmt::format(", {} RECOVERED from front-matter ids",
                                   jots->recovered_orphans())
                     : std::string{});
    m_project = jots.get();
    m_store = std::move(jots);
    core::recents_add(m_recents, dir);
    core::save_recents(m_recents_file, m_recents);
    // The header is repainted by build_ui once the menu model exists; there is
    // one place that writes the title and this is not it.
}

// Re-point every surface at a different jots folder. Same call path as first run, so
// there is only one way a jots folder gets opened.
void Shell::open_jots(const std::string& dir) {  // helper: point the surfaces at a jots folder
    if (m_project) m_project->flush();
    auto jots = std::make_unique<core::Project>();
    if (!jots->open(dir)) {
        if (auto lg = log::get(log::Area::Io)) lg->error("jots folder '{}': cannot open", dir);
        return;
    }
    m_project = jots.get();
    m_store = std::move(jots);
    m_store->on_changed(sigc::mem_fun(*this, &Shell::on_model_changed));
    m_tree->set_source(m_store.get());
    m_editor->set_source(m_store.get());
    m_links.rebuild(*m_store);
    m_drawer->set_source(m_store.get(), &m_links);
    m_drawer->set_jots_dir(m_project ? m_project->dir() : std::string{});
    note_recent(dir);
    update_jots_title();

    auto roots = m_store->children("");
    if (roots.empty()) {
        const auto id = m_store->create("", "");
        if (!id.empty()) roots.push_back(id);
    }
    if (!roots.empty()) { m_tree->select(roots.front()); on_selection_changed(roots.front()); }
    update_note_actions();
    drain_pending();   // a folder just appeared -- whatever was spooled has a home now
}

// An action that can't do anything should not look like an offer. Greying is
// the honest form of "protected" and "nothing selected" -- CANON's "in your
// face" design, applied to the menu rather than to a dialog.
void Shell::update_note_actions() {  // helper: grey what the selection can't do
    const auto id = m_tree ? m_tree->selected() : core::NodeId{};
    const core::Node* n = (!id.empty() && m_store) ? m_store->find(id) : nullptr;
    const bool is_task = n && n->task.is_task;
    if (m_act_new_child) m_act_new_child->set_enabled(n != nullptr);
    if (m_act_toggle_todo) m_act_toggle_todo->set_enabled(n != nullptr && !n->protect);
    if (m_act_toggle_done) m_act_toggle_done->set_enabled(is_task && !n->protect);
    if (m_act_toggle_flag) m_act_toggle_flag->set_enabled(is_task && !n->protect);
    if (m_act_rename_note) m_act_rename_note->set_enabled(n != nullptr && !n->protect);
    if (m_act_protect)   m_act_protect->set_enabled(n != nullptr);
    if (m_act_delete)    m_act_delete->set_enabled(n != nullptr && !n->protect);

    // The ticks (s016c). Written only here, from the model, so the check items
    // in both note menus always say what the selected note IS.
    auto tick = [](const Glib::RefPtr<Gio::SimpleAction>& a, bool on) {
        if (a) a->set_state(Glib::Variant<bool>::create(on));
    };
    tick(m_act_toggle_todo, is_task);
    tick(m_act_toggle_done, is_task && n->task.done);
    tick(m_act_toggle_flag, is_task && n->task.flagged);
    tick(m_act_protect,     n && n->protect);
}

std::string Shell::prefs_file() const {  // helper: XDG path for the layout pump
    // Same resolution as recents_file(): the UI does the XDG work and the core
    // takes a plain string.
    return (std::filesystem::path(Glib::get_user_data_dir()) / "jot" / "prefs.json")
        .string();
}

// ─────────────────────────────────────────────────────────────────────────────
// apply_layout_state -- ONE writer for both panes.
//
// Called by both toggles and once at startup. Sets visibility, the stateful
// actions (so the header buttons and the menu checkmarks agree), and the
// remembered positions -- then persists.
//
// ── THE SCAR ────────────────────────────────────────────────────────────────
// A collapsed Gtk::Paned child loses its position, and GTK does not reclaim the
// split when the child is shown again: the centre stays at its expanded width
// and the returning pane comes back as a sliver. Folio fixes it by re-setting
// the position on IDLE and AGAIN on a 50ms timeout after layout settles.
//
// The double pass is EMPIRICAL, NOT PRINCIPLED. It is lifted here exactly as it
// stands in Folio because it is a RUN reference -- code observed to work on a
// real display -- and a principled guess I cannot run is worth less than a
// scar somebody else earned. It is marked to INVESTIGATE, not to tidy. If the
// panes ever misbehave on reveal, that timeout is the first suspect and this
// comment is the reason it is the first suspect.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::apply_layout_state() {  // helper: the one place layout changes
    m_applying_layout = true;

    // The LEFT PANE, not the tree inside it. F9 hides the whole side -- tabs,
    // tree and Today -- because both-toggles-off is the focus mode and a stray
    // tab bar floating over an empty stack is not it. (s006 built that front
    // door; s007 put a second view behind the same door and nearly left the
    // handle attached to only one of them.)
    m_left.set_visible(m_prefs.show_tree);
    m_drawer->set_visible(m_prefs.show_drawer);

    if (m_act_toggle_tree)
        m_act_toggle_tree->set_state(Glib::Variant<bool>::create(m_prefs.show_tree));
    if (m_act_toggle_drawer)
        m_act_toggle_drawer->set_state(Glib::Variant<bool>::create(m_prefs.show_drawer));

    m_tree_toggle.set_tooltip_text(m_prefs.show_tree ? "Hide the side pane (F9)"
                                                     : "Show the side pane (F9)");
    m_drawer_toggle.set_tooltip_text(m_prefs.show_drawer ? "Hide note details (F10)"
                                                         : "Show note details (F10)");

    const auto restore = [this]() {
        if (m_prefs.show_tree)   m_paned_left.set_position(m_prefs.tree_width);
        if (m_prefs.show_drawer) m_paned_right.set_position(m_prefs.note_width);
    };
    restore();
    Glib::signal_idle().connect_once([this, restore]() {
        m_applying_layout = true;
        restore();
        m_applying_layout = false;
    });
    Glib::signal_timeout().connect_once([this, restore]() {
        m_applying_layout = true;
        restore();
        m_applying_layout = false;
    }, 50);

    m_applying_layout = false;
    core::save_prefs(m_prefs_file, m_prefs);

    if (auto lg = log::get(log::Area::Shell))
        lg->debug("layout: tree={} drawer={} tree_w={} note_w={}",
                  m_prefs.show_tree, m_prefs.show_drawer, m_prefs.tree_width,
                  m_prefs.note_width);
}

// ─────────────────────────────────────────────────────────────────────────────
// queue_drawer_refresh -- coalesce the index update and the repaint to one idle.
//
// Typing fires Change::Body per keystroke. Rescanning the edited body to
// maintain the link index on each of them would put a whole-body markdown scan
// in the keystroke path -- which is the exact cost EditorPane::queue_restyle
// exists to avoid, so it gets the same shape rather than a second answer.
//
// The drawer only has to be right by the time somebody looks at it, and an idle
// is well before that.
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// remember_window_geometry -- what the window looked like, for next launch.
//
// SIZE AND MAXIMIZED, NOT POSITION. GTK4 has no window positioning API and
// Wayland has no concept to expose one over; the compositor places the window.
// See core/Prefs.hpp.
//
// THE UNMAXIMIZED SIZE IS ONLY READABLE WHILE UNMAXIMIZED. `get_width()` on a
// maximized window returns the screen's width, so writing that back would mean
// the next un-maximize gave you a window the size of the display and the size
// you actually chose was gone for good. So when the window is maximized the
// stored size is LEFT ALONE -- it still holds whatever it was before you
// maximized, which is exactly the number un-maximizing should restore.
//
// Called from the close handler rather than from a resize signal: a resize
// fires continuously during a drag, and writing a prefs file per frame to learn
// one number at the end is the wrong trade.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::remember_window_geometry() {  // helper: size + maximized -> prefs
    const bool maxed = is_maximized() || is_fullscreen();
    m_prefs.win_maximized = maxed;
    if (!maxed) {
        // get_default_size(), NOT get_width()/get_height(), and this was
        // measured rather than reasoned: get_width() returns the CONTENT area
        // while set_default_size() takes a size that includes the decorations
        // on some backends, so the two are not the same number. Saving one and
        // restoring through the other lost TEN PIXELS EACH WAY, EVERY LAUNCH --
        // 940x620, then 930x610, then 920x600, compounding for as long as the
        // app is used.
        //
        // get_default_size is set_default_size's own inverse, so the round trip
        // is exact by construction. The general shape: when a value is stored
        // and restored, the getter must be the setter's pair, not merely a
        // getter that returns something with the same name.
        int w = 0, h = 0;
        get_default_size(w, h);
        // A window mid-teardown can report 0; storing that would come back as
        // the clamp's fallback and silently forget a real size.
        if (w > 0 && h > 0) { m_prefs.win_width = w; m_prefs.win_height = h; }
    }
    core::save_prefs(m_prefs_file, m_prefs);

    if (auto lg = log::get(log::Area::Shell))
        lg->debug("window: {}x{} maximized={}", m_prefs.win_width, m_prefs.win_height,
                  m_prefs.win_maximized);
}

// ─────────────────────────────────────────────────────────────────────────────
// queue_tree_rebuild -- rebuild on an IDLE, never inside the event that caused it.
//
// The tree is Pole B: a rebuild DESTROYS every row widget and makes new ones.
// Doing that synchronously from a model change was fine for as long as model
// changes came from the tree itself or from a menu -- and stopped being fine
// the moment a pane that commits on FOCUS-LEAVE existed.
//
// The failing sequence, from a real backtrace:
//
//   1. focus is in `drawer.due`; the user clicks a row in the tree
//   2. the click moves focus, so the entry's focus-leave fires and COMMITS
//   3. the commit is a model write -> on_model_changed -> rebuild
//   4. every row, INCLUDING THE ONE BEING CLICKED, is destroyed
//   5. GTK's click gesture, still mid-flight, grabs focus on that row --
//      which now has no parent, so gtk_widget_get_parent() walks off the top
//
//   Gtk-CRITICAL: gtk_widget_get_parent: assertion 'GTK_IS_WIDGET (widget)' failed
//
// The general rule, and it is not specific to this pane: NEVER DESTROY A WIDGET
// FROM INSIDE AN EVENT BEING DELIVERED TO IT. An idle is the first moment the
// gesture has certainly finished. Coalescing is the free half -- a burst of
// changes costs one rebuild instead of one each.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::queue_tree_rebuild() {  // helper: rebuild on an idle
    if (m_tree_rebuild_queued) return;
    m_tree_rebuild_queued = true;
    Glib::signal_idle().connect_once([this]() {
        m_tree_rebuild_queued = false;
        if (m_tree) m_tree->rebuild();
    });
}

void Shell::queue_drawer_refresh() {  // helper: one index update + repaint per idle
    if (m_drawer_refresh_queued) return;
    m_drawer_refresh_queued = true;
    Glib::signal_idle().connect_once([this]() {
        m_drawer_refresh_queued = false;
        const core::NodeId id = m_editor->current();
        if (!id.empty()) m_links.update(*m_store, id);
        m_drawer->refresh();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// capture -- text in, an unfiled note out, and NOTHING ELSE MOVES.
//
// The one rule that makes this worth having: it does not touch the selection,
// does not load the new note into the editor, and does not present the window.
// You are reading something; a thought arrives; it is filed and you are still
// reading the same thing. A capture that took you somewhere would be a capture
// you learn not to use while you are busy, which is the only time it matters.
//
// The split (title / body, nothing lost) lives in core::capture, so this path
// and `jot --capture` cannot disagree about what a captured line becomes.
//
// The tell is deliberately small. The tree rebuilds and the row appears, which
// is the real feedback -- but in full-focus mode both side panes are hidden and
// nothing would show at all, so the placeholder says what landed.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::capture(const std::string& text) {  // helper: text -> an unfiled note
    if (!m_store) return;
    const core::NodeId id = core::capture(*m_store, text);
    if (id.empty()) return;

    if (const core::Node* n = m_store->find(id)) {
        std::string tell = n->title;
        if (tell.size() > 24) tell = tell.substr(0, 22) + "\u2026";
        m_capture_tell = true;
        m_capture.set_placeholder_text("Captured \u201c" + tell + "\u201d");
    }
    // Structure is written immediately everywhere else in this file and a
    // capture is no different -- the note you typed and walked away from is
    // exactly the one you would not forgive being lost.
    if (m_project) m_project->flush();

    if (auto lg = log::get(log::Area::Model))
        lg->info("capture: '{}' -> {}", text.size() > 40 ? text.substr(0, 40) + "..." : text, id);
}

std::string Shell::pending_dir() const {  // helper: XDG path for the capture spool
    // Same shape as recents_file() and prefs_file(): the UI resolves XDG, the
    // core owns the subpath, and App resolves the identical path the same way.
    return core::pending_dir(Glib::get_user_data_dir());
}

// ─────────────────────────────────────────────────────────────────────────────
// drain_pending -- the thoughts that arrived while jot was not running.
//
// `jot --capture "milk"` with no live instance writes into the spool and exits
// without a window (App::file_pending). This is the other end of that: on the
// way in, whatever is waiting becomes notes, oldest first.
//
// ── ONLY INTO A JOTS FOLDER, AND THAT IS THE LOAD-BEARING LINE ─────────────
// The spool file is deleted once its note exists, so the note has to exist
// somewhere that outlives this process. A scratch buffer does not: draining
// into one would turn a capture that was safely on disk into a capture that
// Discard-on-the-way-out silently takes. With no folder open the spool simply
// waits, and the three callers -- first launch, Open, and the Save that gives
// the scratch buffer a home -- are exactly the three moments a folder appears.
//
// ── AND THE FLUSH COMES BEFORE THE DELETE ──────────────────────────────────
// Same rule one layer down. Every note is written to the jots folder BEFORE any
// spool file is removed, so a crash in the middle costs a duplicate rather than
// a thought. A duplicate is an annoyance; a lost capture is the one failure a
// capture spool does not get to have.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::drain_pending() {  // helper: file what was captured while jot was closed
    if (!m_store || !m_project) return;

    const auto waiting = core::read_pending(pending_dir());
    if (waiting.empty()) return;

    std::vector<core::Pending> filed;
    filed.reserve(waiting.size());
    for (const auto& p : waiting) {
        if (p.text.empty()) { core::remove_pending(p); continue; }   // nothing was said
        // core::capture, the same function the header box uses, so a spooled
        // thought and a typed one become the same kind of note.
        if (core::capture(*m_store, p.text).empty()) continue;        // keep the file
        filed.push_back(p);
    }
    if (filed.empty()) return;

    m_project->flush();                       // on disk BEFORE the only other copy goes
    std::size_t removed = 0;
    for (const auto& p : filed) if (core::remove_pending(p)) ++removed;

    if (auto lg = log::get(log::Area::Io))
        lg->info("drained {} pending capture(s) into '{}'{}", filed.size(), m_project->dir(),
                 removed == filed.size()
                     ? std::string{}
                     : fmt::format(" -- {} spool file(s) SURVIVED and will file again",
                                   filed.size() - removed));

    // The tell. The rows appearing in the tree is the real feedback, but in
    // full-focus mode both side panes are hidden and nothing would show at all
    // -- so the capture line says what landed, exactly as it does for a capture
    // you typed yourself.
    m_capture_tell = true;
    m_capture.set_placeholder_text(
        filed.size() == 1
            ? "Filed 1 note captured while jot was closed"
            : fmt::format("Filed {} notes captured while jot was closed", filed.size()));

    refresh_tasks(true);
    queue_tree_rebuild();
}

// `jot --capture` with nothing to capture, and the Ctrl+Shift+Enter accel. The
// window has to be up and in front before a cursor in it means anything, so
// this presents first and focuses after.
void Shell::focus_capture_bar() {  // helper: put the cursor in the box
    present();
    m_capture.grab_focus();
}

// ─────────────────────────────────────────────────────────────────────────────
// refresh_tasks -- ONE writer for the task index and the report over it.
//
// Every path that can change what is available calls this and nothing else
// touches either. `rebuild_index` says whether the SET of todos or their order
// could have moved (a create, a move, a delete, a reload) as opposed to one
// node's fields changing.
//
// Today is refreshed even when it is not the visible child of the stack. It is
// cheap -- the query is a filter over a short vector -- and the alternative is
// a flag saying "Today is stale", which is a cache, which is the thing this
// whole file refuses to keep.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::refresh_tasks(bool rebuild_index, const core::NodeId& id) {
    if (!m_store) return;
    if (rebuild_index)   m_tasks.rebuild(*m_store);
    else if (!id.empty()) m_tasks.update(*m_store, id);
    if (m_today) m_today->refresh();
    queue_desktop_sync();
}

// ─────────────────────────────────────────────────────────────────────────────
// queue_desktop_sync -- arm the debounce. THE ONLY WAY A SYNC STARTS.
//
// One entry point on purpose: refresh_tasks() calls it and nothing else does,
// so "when does the desktop get rewritten" has a single answer that is true
// everywhere, rather than a set of call sites to keep in agreement.
//
// 1500ms is chosen against how a scribble pad is used, not against a
// responsiveness target: typing a title is a burst of model writes, and the
// interesting question is when the burst STOPS. Re-arming on each change means
// a run of edits costs exactly one sync at the end of it.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::queue_desktop_sync() {  // helper: arm the debounce
    if (!m_prefs.desktop_tasks) return;
    m_desktop_timer.disconnect();
    m_desktop_timer = Glib::signal_timeout().connect(
        [this]() { run_desktop_sync(/*force=*/false); return false; }, 1500);
}

// ─────────────────────────────────────────────────────────────────────────────
// run_desktop_sync -- past the fingerprint, across the process boundary.
//
// The fingerprint check is what makes a sync on every model change affordable.
// A body edit, a rename of an undated note, a tick on something with no due
// date: all of them reach here and none of them reaches EDS.
//
// It is NOT a cache of the answer. The projection is recomputed from the model
// on every call; the only thing skipped is the writing. That distinction is the
// whole of why a stale fingerprint cannot hide a wrong desktop: the worst it
// can do is skip a write that would have produced the same bytes.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::run_desktop_sync(bool force) {  // helper: the sync itself
    if (!m_store || !m_prefs.desktop_tasks) return;

    const auto items = core::project(*m_store, m_tasks, std::time(nullptr));
    const auto sig   = core::projection_signature(items);
    if (!force && sig == m_desktop_sig) return;

    // Recorded BEFORE the call, because the call may come back `pending` and
    // the result that eventually reports success arrives with no memory of
    // which projection it was writing.
    m_desktop_inflight_sig   = sig;
    m_desktop_inflight_count = items.size();

    on_desktop_result(m_desktop.sync(items));
}

// ─────────────────────────────────────────────────────────────────────────────
// on_desktop_result -- ONE landing place for every result the backend produces,
// whether it came back from the call or arrived minutes later through the
// report callback.
//
// One function on purpose. A deferred result is indistinguishable from an
// immediate one except for when it shows up, so giving it its own handler would
// mean two copies of the same status-line wording drifting apart -- and the
// status line is the ONLY feedback for a feature whose effect happens in
// another process.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_desktop_result(const DesktopResult& r) {  // helper: one landing place for every desktop result
    // PENDING is neither success nor failure. Do not record the fingerprint --
    // nothing has been written yet -- and do not report an error, because
    // nothing has gone wrong. Say what is happening and wait.
    if (r.pending) { show_desktop_status(r.message); return; }

    if (!r.ok) {
        // Do NOT record the fingerprint on a failure, or the next identical
        // change would decide there was nothing to do and the desktop would
        // stay wrong until something unrelated moved.
        m_desktop_sig.clear();
        show_desktop_status(r.message.empty() ? "The desktop could not be reached."
                                              : r.message);
        return;
    }

    if (r.op == DesktopOp::Clear) {
        // A clear must never commit the fingerprint a sync was hoping for. The
        // desktop is now EMPTY, which is not what any projection looks like.
        m_desktop_sig.clear();
        show_desktop_status("Off. The desktop calendar is empty.");
        return;
    }

    m_desktop_sig = m_desktop_inflight_sig;

    char when[6] = "--:--";
    const std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_r(&t, &lt);
    std::strftime(when, sizeof when, "%H:%M", &lt);

    std::string msg;
    if (m_desktop_inflight_count == 0)
        msg = std::string("Nothing dated to show. Last checked ") + when + ".";
    else
        msg = std::to_string(r.written) +
              (r.written == 1 ? " todo on the desktop" : " todos on the desktop") +
              ", at " + when + ".";
    if (!r.message.empty()) msg += "  " + r.message;
    show_desktop_status(msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// The notification half. A projection is state; a notification is an EVENT.
//
// That is why this needs a CLOCK and the projection does not: nothing in the
// model changes when a deadline simply arrives, so there is no callback to hang
// this off. refresh_tasks() cannot help -- it fires when the user does
// something, and the whole point is the moment when the user does nothing.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::start_notify_timer() {  // helper: the minute clock behind due notifications
    m_notify_timer.disconnect();
    if (!m_prefs.notify_due) return;
    m_notify_timer = Glib::signal_timeout().connect_seconds(
        [this]() { check_due_notifications(); return true; }, 60);
}

void Shell::check_due_notifications() {  // helper: announce what has just come due
    if (!m_store || !m_prefs.notify_due || !m_notify_ok) return;

    const auto r = core::due_announcements(*m_store, m_tasks, std::time(nullptr),
                                           m_prefs.announced);

    // The announced set is rewritten even when nothing is shown, because
    // PRUNING is half of what it does: a todo that was ticked off or
    // rescheduled has to leave, or the list grows for the life of the jots
    // folder and a re-opened task never announces again.
    //
    // What is NOT done here any more is adding to it. s011 wrote
    // `m_prefs.announced = r.keep` with r.keep holding every due key, sent or
    // not -- so a deadline became "announced" at the moment jot decided to
    // speak, and a send the daemon dropped was recorded as said, once, for good.
    // Now a key joins that set in on_notify_receipt() and nowhere else.
    const bool moved = (r.keep != m_prefs.announced);
    m_prefs.announced = r.keep;
    if (moved) core::save_prefs(m_prefs_file, m_prefs);

    // The ledger holds the gap between asking and being answered, so it is
    // pruned against the same live set: a todo ticked off mid-flight must not
    // leave a try-count behind for a rescheduled version to inherit.
    m_outbox.prune(r.live);
    m_notify_live_n = r.live.size();

    for (const auto& a : r.to_show) {
        // Already asked and not yet answered. The tick is every sixty seconds
        // and a slow daemon must not be asked twice for one row.
        if (!m_outbox.begin(a.key)) continue;

        Notice n;
        n.key     = a.key;
        // Keyed by the NODE id, so a second notification about the same task
        // replaces the first rather than stacking. Nobody wants four rows in
        // the tray for one deadline. (Our key carries the due date; the tray's
        // must not, or a rescheduled task leaves the old row sitting there.)
        n.tray_id = a.id;
        n.title   = a.summary;
        n.body    = a.detail;
        n.icon    = "jot-logo-symbolic";
        // ── the click, and why the action is on the APPLICATION ─────────────
        // A notification outlives the window that sent it: GNOME keeps it in
        // the message tray, and clicking it may well happen after jot has been
        // closed. The daemon activates the APPLICATION by id and dispatches
        // into its action group, so a "win." action would have nothing to land
        // in. App owns `goto-node` and forwards to the Shell it builds.
        n.action  = "app.goto-node";
        n.target  = a.id;
        // Urgency, not decoration: an overdue deadline should survive "Do Not
        // Disturb" and a deadline arriving on schedule should not.
        n.urgent  = a.overdue;

        if (auto lg = log::get(log::Area::Shell))
            lg->info("notify: asking for '{}' ({}) id={} try={}", a.summary,
                     a.overdue ? "overdue" : "due", a.id, m_outbox.tries(a.key) + 1);

        m_notifier.send(n);
    }

    show_notify_status();
}

// ─────────────────────────────────────────────────────────────────────────────
// on_notify_receipt -- THE MILESTONE. What the daemon said back.
//
// A deadline is not announced until this runs and says it landed. That one rule
// turns the sixty-second tick into the retry loop, with no retry machinery
// anywhere: a key that never lands is never in the announced set, so
// due_announcements offers it again next minute, and again, until it goes or
// the tries run out.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_notify_receipt(const Receipt& rec) {  // helper: what the daemon said back
    auto lg = log::get(log::Area::Shell);

    // ── whose receipt is this? ────────────────────────────────────────────
    // The outbox holds exactly the sends whose ANSWER CHANGES STATE. The
    // diagnostic in the menu sends four notifications that were never put in
    // it, on purpose: they must not be able to mark a real deadline said.
    const bool ours = m_outbox.in_flight(rec.notice.key);

    m_notify_last_at    = std::time(nullptr);
    m_notify_last_title = rec.notice.title;

    switch (rec.outcome) {
    case Receipt::Outcome::Delivered:
        m_notify_verified = true;
        m_notify_last_error.clear();
        ++m_notify_delivered;
        if (lg) lg->info("notify: DELIVERED '{}' key={}", rec.notice.title, rec.notice.key);
        if (ours) {
            m_outbox.succeed(rec.notice.key);
            commit_announced(rec.notice.key);
        }
        break;

    case Receipt::Outcome::NoService:
        // Nobody owns org.gtk.Notifications here -- another desktop, or GNOME
        // without it. Not a refusal and not ours to argue with: take the road
        // with no receipt on it, and say on the status line that the confirmation
        // is missing rather than pretending there was one.
        if (lg)
            lg->warn("notify: no org.gtk.Notifications -- sending unreceipted ({})",
                     rec.message);
        send_unreceipted(rec.notice);
        m_notify_verified   = false;
        m_notify_last_error = rec.message;
        if (ours) {
            // Marked said despite no confirmation. The alternative is asking
            // every sixty seconds forever on a desktop that will never answer,
            // which is not honesty, it is a loop.
            m_outbox.succeed(rec.notice.key);
            commit_announced(rec.notice.key);
        }
        break;

    case Receipt::Outcome::Refused:
        m_notify_verified   = false;
        m_notify_last_error = rec.message;
        if (lg) lg->error("notify: REFUSED '{}' -- {}", rec.notice.title, rec.message);
        if (ours && m_outbox.fail(rec.notice.key) == core::Outbox::After::GiveUp) {
            if (lg)
                lg->error("notify: giving up on '{}' after {} tries",
                          rec.notice.title, core::kDeliveryTries);
            commit_announced(rec.notice.key);   // stop asking; it is not going to go
        }
        break;
    }

    show_notify_status();
}

// A key enters the announced set HERE and nowhere else -- one writer, and the
// thing it writes is "somebody has this," not "we tried."
void Shell::commit_announced(const std::string& key) {  // helper: a deadline is said ONCE it has landed
    if (std::find(m_prefs.announced.begin(), m_prefs.announced.end(), key) !=
        m_prefs.announced.end())
        return;
    m_prefs.announced.push_back(key);
    core::save_prefs(m_prefs_file, m_prefs);
}

// ── the unreceipted road ────────────────────────────────────────────────────
// g_application_send_notification: void, no reply, no way to know. Kept for the
// desktops that have no org.gtk.Notifications, because a notification with no
// proof still beats no notification -- and NOT used anywhere else, which is the
// point of the milestone.
//
// Gio::Application::get_default(), never get_application(): a HIDDEN WINDOW HAS
// NO APPLICATION (s012), and that null is what swallowed every send jot made
// while resident. The thing that outlives the window must not be reached
// through the window.
void Shell::send_unreceipted(const Notice& notice) {  // helper: the road with no receipt on it
    auto app = Gio::Application::get_default();
    if (!app) {
        if (auto lg = log::get(log::Area::Shell))
            lg->error("notify: NOT SENT '{}' -- no default application", notice.title);
        return;
    }
    auto n = Gio::Notification::create(notice.title);
    if (!notice.body.empty()) n->set_body(notice.body);
    if (!notice.icon.empty()) n->set_icon(Gio::ThemedIcon::create(notice.icon));
    if (!notice.action.empty())
        n->set_default_action_variant(
            notice.action, Glib::Variant<Glib::ustring>::create(notice.target));
    n->set_priority(notice.urgent ? Gio::Notification::Priority::URGENT
                                  : Gio::Notification::Priority::NORMAL);
    app->send_notification(notice.tray_id, n);
}

// ─────────────────────────────────────────────────────────────────────────────
// on_test_notify -- LOG MODE, for a notification that is sent and never seen.
//
// s012's notifications are logged as sent and do not arrive. A manual
// `AddNotification` over the bus carrying jot's own application id DOES arrive,
// so the id resolves and the daemon is healthy -- which leaves the CONTENT of
// what jot builds, and exactly two fields separate the two cases: the icon and
// the default action.
//
// So this sends four, differing by one field each. Whichever ones appear name
// the field that kills it, in one click. The point is the FEEDBACK LOOP as much
// as the bisect: the only way to test this until now was to set a due date and
// wait up to sixty seconds per attempt, which is how an evening goes.
//
// It is a diagnostic and it lives with the other two dumps in the menu. When the
// answer is known it comes out again.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_test_notify() {  // handler: four notifications, one field apart
    auto app = Gio::Application::get_default();
    auto lg = log::get(log::Area::Shell);
    if (lg)
        lg->info("test-notify: window-app={} default-app={}",
                 get_application() ? "present" : "NULL",
                 app ? "present" : "NULL");
    if (!app) {
        if (lg) lg->error("test-notify: NO APPLICATION -- send_notification had nowhere to go");
        show_notify_status();
        return;
    }

    // A real node id, so the action variant is the same shape a due
    // notification sends rather than a synthetic one the handler would reject.
    std::string target = "test";
    if (m_store) {
        const auto roots = m_store->children("");
        if (!roots.empty()) target = roots.front();
    }

    if (lg)
        lg->info("test-notify: app={} registered={} target='{}' notify_ok={}",
                 app->get_id().raw(), app->is_registered(), target, m_notify_ok);

    struct Variant { const char* id; const char* title; bool icon; bool action; };
    const Variant variants[] = {
        {"jot-test-plain",  "1 of 4: plain",        false, false},
        {"jot-test-icon",   "1 of 4: with icon",    true,  false},
        {"jot-test-action", "3 of 4: with action",  false, true },
        {"jot-test-both",   "4 of 4: icon+action",  true,  true },
    };

    // ── s015: the same four, but down the RECEIPTED road ──────────────────
    // The instrument got sharper for free. "Count how many arrive" was the only
    // available reading because the send could not report anything; now each of
    // the four comes back with the daemon's own answer, so a variant that is
    // REFUSED says so in the log with the reason, and a variant that is
    // delivered and still invisible is a different bug entirely -- which is the
    // distinction the whole evening of s012 was missing.
    //
    // These keys are NOT put in the outbox, deliberately: a diagnostic must not
    // be able to mark a real deadline announced. on_notify_receipt checks.
    for (const auto& v : variants) {
        Notice n;
        n.key     = std::string("diagnostic:") + v.id;
        n.tray_id = v.id;
        n.title   = v.title;
        n.body    = "If you can see this one, that combination survives.";
        if (v.icon)   n.icon = "jot-logo-symbolic";
        if (v.action) { n.action = "app.goto-node"; n.target = target; }
        m_notifier.send(n);
        if (lg) lg->info("test-notify: asked '{}' icon={} action={}", v.id, v.icon, v.action);
    }

    m_today->set_notify_status(
        "Four test notifications asked for. The log carries what the daemon said "
        "to each; count how many ARRIVE \u2014 a delivered one you cannot see is a "
        "different bug from a refused one.");
}

// ─────────────────────────────────────────────────────────────────────────────
// show_notify_status -- and the one thing it exists to say.
//
// A notification whose application id the daemon cannot look up is DROPPED
// SILENTLY. jot runs out of a build tree with no installed .desktop entry, so
// the default state of this feature on a developer's machine is "works
// perfectly, shows nothing" -- indistinguishable from a bug, and unfindable
// from inside the app. So jot asks whether the entry is there and says so.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::show_notify_status() {  // helper: the footer's fourth line
    if (!m_today) return;
    if (!m_prefs.notify_due) {
        m_today->set_notify_status("Off. Due dates stay inside jot.");
        return;
    }
    if (!m_notify_ok) {
        m_today->set_notify_status(
            "Not installed \u2014 GNOME drops notifications from an app it cannot "
            "look up. Run  ./build.sh --install-desktop  once, then restart jot.");
        return;
    }
    // ── receipts, with a clock on them (s015) ─────────────────────────────
    // s011 printed the size of the announced set, which is a count of KEYS, and
    // the line that replaced it timed the SEND -- the moment jot stopped
    // knowing anything. Neither could tell "GNOME showed it" from "GNOME ate
    // it."
    //
    // This one says DELIVERED, and it is allowed to say it because the daemon
    // answered. The word is the whole difference: every line above it in this
    // file used to be able to report a success for a verb that cannot fail.
    const std::size_t open = m_notify_live_n;
    const std::string watching =
        open == 0 ? std::string("Nothing is due.")
                  : std::to_string(open) +
                    (open == 1 ? " open deadline." : " open deadlines.");

    // Asked and not yet answered. Usually a blink; visible only when the daemon
    // is slow or gone, which is exactly when it is worth seeing.
    const std::size_t waiting = m_outbox.size();

    if (m_notify_last_at == 0) {
        // Nothing has been ANSWERED in this process. Said that way rather than
        // "nothing announced", because the announced set survives a restart and
        // the keys in it were confirmed to a previous run.
        std::string s = "On. " + watching;
        if (waiting)   s += "  Waiting on the notification service\u2026";
        else if (open) s += " Nothing delivered this run.";
        m_today->set_notify_status(s);
        return;
    }

    char when[16] = "--:--";
    std::tm lt{};
    localtime_r(&m_notify_last_at, &lt);
    std::strftime(when, sizeof when, "%H:%M", &lt);

    const std::string quoted = "\u201c" + m_notify_last_title + "\u201d";
    std::string head;
    if (m_notify_verified) {
        head = "Delivered " + quoted + " at " + std::string(when) + ".";
        if (m_notify_delivered > 1)
            head += "  " + std::to_string(m_notify_delivered) + " confirmed this run.";
    }
    else if (m_notify_last_error.empty())
        head = "Sent " + quoted + " at " + std::string(when) + " \u2014 unconfirmed.";
    else
        head = "Sent " + quoted + " at " + std::string(when) +
               " \u2014 no confirmation: " + m_notify_last_error;

    std::string s = "On. " + head + "  " + watching;
    if (waiting)
        s += "  Retrying " + std::to_string(waiting) +
             (waiting == 1 ? " deadline." : " deadlines.");
    m_today->set_notify_status(s);
}

void Shell::show_desktop_status(const std::string& s) {  // helper: the footer's second line
    if (m_today) m_today->set_desktop_status(s);
}

// ─────────────────────────────────────────────────────────────────────────────
// RESIDENCY (s012). jot stays running when its window is closed, so the clock
// above keeps ticking.
//
// It is not a daemon and it did not need to be. g_application_hold() is the
// whole mechanism: no autostart, no second lifecycle, no supervision, nothing
// installed beyond what s011 already installed. GNOME lists a held application
// with no windows under Background Apps in the system menu, with a Quit item --
// which is what makes this honest rather than lurking. The user can SEE that
// jot is running and stop it, in the same place they see everything else that
// does this.
// ─────────────────────────────────────────────────────────────────────────────

// ONE WRITER for the hold, and it is idempotent. GApplication COUNTS holds and
// releases; two holds and one release is a process that never exits, and one
// release too many takes the count negative under a window that is still open.
// m_holding is the mirror that makes calling this twice harmless.
void Shell::apply_background_hold() {  // helper: ONE writer for the application hold
    auto app = get_application();
    if (!app) return;
    const bool want = m_prefs.background && !m_quitting;
    if (want == m_holding) return;
    if (want) app->hold(); else app->release();
    m_holding = want;
}

core::Lifecycle Shell::lifecycle_state() const {  // helper: what the pure decision sees
    core::Lifecycle s;
    s.background = m_prefs.background;
    s.quitting   = m_quitting;
    s.forced     = m_force_close;
    s.scratch    = scratch_has_content();
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// request_quit -- the one door out of the process, and the real work of s012.
//
// Under residency the scratch prompt has to MOVE. It hangs off the close today
// because close means exit; once close means "keep running" that prompt is a
// lie -- nothing is being lost -- and the moment work is actually at risk is
// this one. But this path has NO WINDOW TO PUT A DIALOG ON: the quit may be
// arriving from GNOME's Background Apps list at a jot whose window was closed
// an hour ago.
//
// So it presents the window and asks there. A quit that stops to ask is
// startling exactly once, and the alternative is discarding an afternoon's
// notes because the process happened to have no surface at the time. The window
// is the only honest place for the question, so the quit goes and gets it.
//
// Three callers: win.quit (the menu and Ctrl+Q), app.quit (GNOME's Background
// Apps Quit, over D-Bus), and the close handler when residency is off and the
// prompt has been answered.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::request_quit() {  // helper: ask what needs asking, then go
    if (core::on_quit(lifecycle_state()) == core::OnQuit::AskFirst) {
        if (auto lg = log::get(log::Area::Shell))
            lg->info("quit requested with unsaved notes -- asking");
        present();               // there has to BE a window to ask on
        guard_scratch([this]() { m_force_close = true; finish_quit(); });
        return;
    }
    finish_quit();
}

// Past every question. Flush, stop holding, and let the window go -- the app
// ends when its last window does, which runs the ordinary teardown rather than
// g_application_quit()'s hard stop past every destructor in the process.
void Shell::finish_quit() {  // helper: past the prompt -- flush, release, go
    m_quitting = true;
    if (get_visible()) remember_window_geometry();   // a hidden window reports nothing useful
    if (m_project) m_project->flush();
    apply_background_hold();                         // m_quitting makes this a release
    if (auto lg = log::get(log::Area::Shell)) lg->info("quitting");
    close();
}

// ─────────────────────────────────────────────────────────────────────────────
// show_background_status -- the footer's sixth line.
//
// It says the CONSEQUENCE, not the setting: the check box already carries the
// setting. Three things can be true at once here and each of them is a thing
// somebody would otherwise find out by accident --
//
//   * with residency off, a closed jot announces nothing. That is the entire
//     argument for the switch and the status line above it cannot say it;
//   * the same .desktop entry a notification is routed through is what GNOME
//     looks jot up in to list it under Background Apps. One lookup, two
//     features, and the same silent failure if it is missing;
//   * a resident jot with no jots folder is holding an unsaved scratch buffer
//     in memory INDEFINITELY, with no window to show it in. Probably fine; it
//     should still be said out loud before it surprises someone.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::show_background_status() {  // helper: the footer's sixth line
    if (!m_today) return;
    if (!m_prefs.background) {
        m_today->set_background_status(
            m_prefs.notify_due
                ? "Off. Closing the window quits jot, and a closed jot announces nothing."
                : "Off. Closing the window quits jot.");
        return;
    }
    // s016a: the Background Apps promise is gone from both lines. s012 LOOKED,
    // and an unsandboxed jot is not listed there -- so a status line that sent
    // the user to that menu to quit was sending them somewhere jot is not.
    if (!m_notify_ok) {
        m_today->set_background_status(
            "On \u2014 but jot has no installed .desktop entry, so its notifications "
            "go nowhere. Run  ./build.sh --install-desktop  once.");
        return;
    }
    std::string s = "On. Closing the window hides it; the clock keeps running. "
                    "Quit from jot's menu, or Ctrl+Q.";
    if (scratch_has_content())
        s += "  These notes are not on disk \u2014 they are held in memory until you quit.";
    m_today->set_background_status(s);
}

}  // namespace jot
