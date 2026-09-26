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
    menu->append_section(todo);

    auto name = Gio::Menu::create();
    name->append("Rename", "win.rename-note");
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
