#include "Menus.hpp"

namespace jot::menus {

Glib::RefPtr<Gio::Menu> note_menu() {
    auto menu = Gio::Menu::create();

    auto make = Gio::Menu::create();
    make->append("New note", "win.new-note");
    make->append("New child note", "win.new-child");
    make->append("Import Markdown Files\u2026", "win.import-md");            // s021b
    make->append("Import Markdown Folder\u2026", "win.import-md-folder");    // s021c
    menu->append_section(make);

    // A todo IS a note (D2), so its three states are note verbs and live here,
    // not in a menu of their own.
    auto todo = Gio::Menu::create();
    todo->append("Todo", "win.toggle-todo");
    todo->append("Done", "win.toggle-done");
    todo->append("Flagged", "win.toggle-flag");
    todo->append("In Inbox", "win.toggle-inbox");   // s028
    // s031. Where a project stands. A submenu, not four more rows here: most
    // notes are not projects, and the menu should not shout at every one.
    auto project = Gio::Menu::create();
    project->append("Active", "win.project-state::active");
    project->append("On Hold", "win.project-state::on-hold");
    project->append("Completed", "win.project-state::completed");
    project->append("Dropped", "win.project-state::dropped");
    todo->append_submenu("Project", project);
    menu->append_section(todo);

    auto name = Gio::Menu::create();
    name->append("Rename", "win.rename-note");
    name->append("Move to\u2026", "win.move-to");   // s029
    name->append("Copy link", "win.copy-link");
    menu->append_section(name);

    // Last and together: the two that change what else you are allowed to do.
    auto guard = Gio::Menu::create();
    guard->append("Protected", "win.toggle-protect");
    guard->append("Delete", "win.delete-note");
    menu->append_section(guard);

    return menu;
}

}  // namespace jot::menus
