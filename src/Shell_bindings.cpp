#include "Shell.hpp"
#include "DrawerPane.hpp"
#include "EditorPane.hpp"
#include "TreePane.hpp"
#include "TodayPane.hpp"
#include "Log.hpp"
#include "core/Recents.hpp"

#include <giomm/simpleactiongroup.h>
#include <glibmm/variant.h>

// Shell_bindings.cpp -- BINDINGS. What's wired to what: the win.* actions, the
// parameterised recents action group, the tree's selection signal, and the
// model's change callback. No slot bodies here -- those are handlers.

namespace jot {

void Shell::bind_actions() {  // bindings: actions + recents group + model callback
    // Plain win.* actions -> handler bodies (Shell_handlers.cpp).
    add_action("new-note",      sigc::mem_fun(*this, &Shell::on_new_note));
    add_action("dump-nodes",    sigc::mem_fun(*this, &Shell::on_dump_nodes));
    add_action("new-jots",      sigc::mem_fun(*this, &Shell::on_new_jots));
    add_action("open-jots",     sigc::mem_fun(*this, &Shell::on_open_jots));
    add_action("save-all",      sigc::mem_fun(*this, &Shell::on_save_all));
    add_action("save-as",       sigc::mem_fun(*this, &Shell::on_save_as));
    add_action("dump-registry", sigc::mem_fun(*this, &Shell::on_dump_registry));
    add_action("test-notify",   sigc::mem_fun(*this, &Shell::on_test_notify));
    add_action("about",         sigc::mem_fun(*this, &Shell::on_about));
    add_action("shortcuts",     sigc::mem_fun(*this, &Shell::on_shortcuts));
    // NOT close(), and the difference only appeared in s012: under residency a
    // close HIDES the window, so a Quit that closed would leave the process
    // running and the menu item doing nothing visible. Quit means quit, which
    // is a different question from what the X does, and request_quit() is the
    // one place that answers it (Shell_helpers.cpp).
    add_action("quit",          sigc::mem_fun(*this, &Shell::request_quit));
    add_action("copy-link",     [this]() { on_copy_link(m_editor->current()); });
    // Capture has no handler of its own: the accel and the menu item both mean
    // "put the cursor in the box", and Enter in the box is what captures.
    add_action("capture",       sigc::mem_fun(*this, &Shell::focus_capture_bar));

    // The two toggles. STATEFUL actions, so the menu items draw a checkmark and
    // the header buttons draw pressed -- one source of truth for a thing with
    // two consumers, which is the shortcut registry's lesson applied to view
    // state. apply_layout_state() is the only writer.
    m_act_toggle_tree = add_action_bool(
        "toggle-tree", sigc::mem_fun(*this, &Shell::on_toggle_tree), m_prefs.show_tree);
    m_act_toggle_drawer = add_action_bool(
        "toggle-drawer", sigc::mem_fun(*this, &Shell::on_toggle_drawer), m_prefs.show_drawer);
    // s009. Stateful for the same reason the two above are: the menu item and
    // the Today footer's check box are TWO CONSUMERS of one piece of state, and
    // the action is the thing they both read. A bool on the Shell with two
    // widgets setting it is how the menu ends up ticked and the box does not.
    m_act_toggle_desktop = add_action_bool(
        "toggle-desktop", sigc::mem_fun(*this, &Shell::on_toggle_desktop),
        m_prefs.desktop_tasks);
    add_action("desktop-sync", sigc::mem_fun(*this, &Shell::on_desktop_sync_now));
    m_act_toggle_notify = add_action_bool(
        "toggle-notify", sigc::mem_fun(*this, &Shell::on_toggle_notify),
        m_prefs.notify_due);
    m_act_toggle_background = add_action_bool(
        "toggle-background", sigc::mem_fun(*this, &Shell::on_toggle_background),
        m_prefs.background);

    // Held actions: these three are only offers when the selection permits
    // them, so update_note_actions() can grey them (helpers).
    m_act_new_child = add_action("new-child",      sigc::mem_fun(*this, &Shell::on_new_child));
    m_act_delete    = add_action("delete-note",    sigc::mem_fun(*this, &Shell::on_delete_note));
    m_act_protect   = add_action("toggle-protect", sigc::mem_fun(*this, &Shell::on_toggle_protect));
    m_act_rename_note = add_action("rename-note", sigc::mem_fun(*this, &Shell::on_rename_note));

    // The three todo verbs. Two of them grey themselves on a note that is not
    // a todo -- a "tick" that does nothing on the thing you pointed at is the
    // kind of silence that teaches people the app is broken.
    m_act_toggle_todo = add_action("toggle-todo", sigc::mem_fun(*this, &Shell::on_toggle_todo));
    m_act_toggle_done = add_action("toggle-done", sigc::mem_fun(*this, &Shell::on_toggle_done));
    m_act_toggle_flag = add_action("toggle-flag", sigc::mem_fun(*this, &Shell::on_toggle_flag));

    // Which half of the left pane shows. Stateful string, exactly like the two
    // pane toggles are stateful bools -- the tab buttons and the View menu's
    // radio items are two consumers of one state.
    m_act_left_view = add_action_radio_string(
        "left-view", sigc::mem_fun(*this, &Shell::on_left_view), "notes");

    // The location verbs, held for the same reason: with no jots folder open
    // (JOT_STRESS, or a first run still being answered) none of the three is
    // an offer, and update_jots_title() is the one place that decides.
    m_act_open_in_files = add_action("open-in-files",  sigc::mem_fun(*this, &Shell::on_open_in_files));
    m_act_copy_path     = add_action("copy-path",      sigc::mem_fun(*this, &Shell::on_copy_path));
    m_act_relocate      = add_action("relocate-jots",  sigc::mem_fun(*this, &Shell::on_relocate_jots));
    m_act_rename        = add_action("rename-jots",    sigc::mem_fun(*this, &Shell::on_rename_jots));

    // Recents: a "recents" action group with a PARAMETERISED open verb --
    // recents.open(<path>) carries each entry's path as its target, so one
    // action serves every menu row (CANON/Curvz: the proven parameterised-action
    // shape). clear is held so rebuild_recents_menu can grey it when empty.
    auto group = Gio::SimpleActionGroup::create();
    group->add_action_with_parameter(
        "open", Glib::Variant<Glib::ustring>::variant_type(),
        [this](const Glib::VariantBase& param) {
            const std::string p =
                Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(param).get();
            on_recent_open(p);
        });
    m_recents_clear = Gio::SimpleAction::create("clear");
    m_recents_clear->signal_activate().connect(
        [this](const Glib::VariantBase&) { on_recent_clear(); });
    group->add_action(m_recents_clear);
    insert_action_group("recents", group);

    // The two directions of the surface<->model loop, wired once:
    //   tree row picked  -> the editor shows that node
    //   model changed    -> whichever surfaces the change can invalidate repaint
    // The second is what makes drag-and-drop correct: nothing rearranges a
    // widget in response to a drop; the model announces and the tree repaints.
    m_tree->signal_selected().connect(
        sigc::mem_fun(*this, &Shell::on_selection_changed));
    m_store->on_changed(sigc::mem_fun(*this, &Shell::on_model_changed));

    // The drawer does not know the tree exists. It says "go here" and the Shell
    // decides what that means -- which is why a backlink click can reveal a
    // collapsed ancestor without the drawer knowing what collapsing is.
    m_drawer->signal_goto().connect(sigc::mem_fun(*this, &Shell::on_goto_note));
    // Today says "go here" the same way the drawer does, and the Shell answers
    // it the same way -- reveal in the tree, load the note. Neither pane knows
    // the other exists.
    m_today->signal_goto().connect(sigc::mem_fun(*this, &Shell::on_goto_note));
    // The footer asks; the action decides. Routing the box through the ACTION
    // rather than through on_toggle_desktop() directly is what keeps the menu
    // item in step without either widget knowing the other exists.
    m_today->signal_notify_toggled().connect([this](bool) {
        activate_action("win.toggle-notify");    // the "win." prefix -- see below
    });
    m_today->signal_background_toggled().connect([this](bool) {
        activate_action("win.toggle-background");
    });
    m_today->signal_desktop_toggled().connect([this](bool) {
        // "win." PREFIXED, and it matters: Gtk::Widget::activate_action looks
        // the name up in the action MUXER, where window actions live under
        // "win." and application ones under "app.". A bare "toggle-desktop"
        // finds nothing, returns false, and the box ticks while nothing
        // happens -- silently, because a failed activation is a return value
        // nobody was reading.
        activate_action("win.toggle-desktop");
    });
    m_drawer->signal_copy_link().connect(sigc::mem_fun(*this, &Shell::on_copy_link));
}

}  // namespace jot
