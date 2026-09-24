#include "TreePane.hpp"
#include "Log.hpp"
#include "Menus.hpp"
#include "core/Tasks.hpp"

#include <glibmm/markup.h>

#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/listboxrow.h>

#include <gdk/gdkkeysyms.h>

#include <chrono>

// TreePane.cpp -- GLUE + ZONES. Construction, the full rebuild, and one row.
// The drag-and-drop half lives in TreePane_dnd.cpp (index: TreePane.hpp).

namespace jot {

TreePane::TreePane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL),
      m_scroll("tree.scroll"),
      m_list("tree.list"),
      m_menu("tree.menu") {
    m_list.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_list.add_css_class("navigation-sidebar");
    m_scroll.set_child(m_list);
    m_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    append(m_scroll);

    m_list.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (m_rebuilding || !row) return;
        const auto i = static_cast<std::size_t>(row->get_index());
        if (i >= m_row_ids.size()) return;
        m_selected = m_row_ids[i];
        m_sig_selected.emit(m_selected);
    });

    // ── Delete, while the TREE has focus (s016c) ────────────────────────────
    // Bound here, on the list, rather than as an application accelerator. An
    // app accel is heard before the focused widget, so the old app-wide
    // Ctrl+Delete fired from inside the note body -- where a text box uses it
    // to delete a word -- and deleted the note instead. A key on the list only
    // hears keys the list is focused for, which is the Files convention and
    // the whole fix. Bubble phase, and never during an inline rename: the
    // rename entry is a descendant, and Delete there is a character.
    auto del = Gtk::EventControllerKey::create();
    del->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) {
            if (keyval != GDK_KEY_Delete && keyval != GDK_KEY_KP_Delete) return false;
            if (renaming()) return false;
            const auto mods = state & (Gdk::ModifierType::CONTROL_MASK |
                                       Gdk::ModifierType::SHIFT_MASK |
                                       Gdk::ModifierType::ALT_MASK);
            if (mods != Gdk::ModifierType{}) return false;
            if (m_selected.empty()) return false;
            if (auto lg = log::get(log::Area::Tree))
                lg->info("Delete in the tree on {}", m_selected);
            activate_action("win.delete-note");
            return true;
        }, false);
    m_list.add_controller(del);

    build_row_menu();
    attach_root_drop();
}

// ─────────────────────────────────────────────────────────────────────────────
// The row context menu. Right-click selects the row FIRST and then opens, so
// every item acts on what you pointed at rather than on whatever happened to be
// selected before -- a menu that silently acts on something else is a delete
// you did not mean.
//
// The model is built once and parented to the ListBox, which is what lets it
// resolve `win.*` up the widget hierarchy to the window's action group. Nothing
// here duplicates a handler.
// ─────────────────────────────────────────────────────────────────────────────
void TreePane::build_row_menu() {
    // The same model the header's note button shows (Menus.cpp, s016c), so
    // the two menus cannot drift apart again.
    m_menu_model = menus::note_menu();

    m_menu.set_menu_model(m_menu_model);
    // ── PARENTED TO THE PANE, NOT TO THE LIST ───────────────────────────────
    // set_parent() makes a popover a real widget child of its parent, and
    // rebuild() clears the list with `while (get_first_child()) remove()`. A
    // ListBox refuses to remove a child that is not a row, so the popover came
    // back as the first child forever and the rebuild spun. It cost one run
    // headless to find and would have cost a frozen window to find by eye.
    //
    // The pane's own Box has no such loop, so the popover lives there and the
    // pointer coordinates are translated into its space below.
    m_menu.set_parent(*this);
    m_menu.set_has_arrow(false);
    m_menu.set_halign(Gtk::Align::START);

    // Right-click anywhere in the pane. On a row it selects that row; on empty
    // space below the rows it clears the selection, so "New child note"
    // greys itself out and the menu describes what it will actually do.
    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_SECONDARY);
    click->signal_pressed().connect([this](int, double x, double y) {
        if (auto* row = m_list.get_row_at_y(static_cast<int>(y))) {
            const auto i = static_cast<std::size_t>(row->get_index());
            if (i < m_row_ids.size()) {
                m_selected = m_row_ids[i];
                m_list.select_row(*row);
                m_sig_selected.emit(m_selected);
            }
        }
        popup_row_menu(x, y);
    });
    m_list.add_controller(click);
}

// The gesture reports coordinates in the LIST's space and the popover points in
// the PANE's space, and the two differ by the scroll offset -- so a menu opened
// after scrolling would appear above the row it belongs to. One translation,
// and a fall back to the raw point if the widgets are not both realised yet.
void TreePane::popup_row_menu(double x, double y) {
    double px = x, py = y;
    m_list.translate_coordinates(*this, x, y, px, py);   // false leaves px/py alone
    m_menu.set_pointing_to(Gdk::Rectangle(static_cast<int>(px), static_cast<int>(py), 1, 1));
    m_menu.popup();
}

TreePane::~TreePane() { m_menu.unparent(); }

void TreePane::set_source(core::NodeSource* src) {
    m_src = src;
    m_selected.clear();
    m_collapsed.clear();
    rebuild();
}

bool TreePane::is_collapsed(const core::NodeId& id) const {
    return m_collapsed.count(id) != 0;
}

void TreePane::toggle_collapsed(const core::NodeId& id) {
    if (!m_collapsed.erase(id)) m_collapsed.insert(id);
    rebuild();
}

// The full repaint. Everything the user did to the tree -- drag, create,
// delete, rename, collapse -- lands here, because the model is the truth and
// this is only a renderer over it. The timing line is the D6 instrument.
void TreePane::rebuild() {
    const auto t0 = std::chrono::steady_clock::now();

    m_rebuilding = true;
    while (Gtk::Widget* child = m_list.get_first_child()) m_list.remove(*child);
    m_row_ids.clear();

    if (m_src) append_rows("", 0);

    // Restore the selection if the node is still visible. Not a user choice, so
    // it must not re-emit -- hence the flag rather than a "last id" compare.
    if (!m_selected.empty()) {
        for (std::size_t i = 0; i < m_row_ids.size(); ++i)
            if (m_row_ids[i] == m_selected) {
                if (auto* row = m_list.get_row_at_index(static_cast<int>(i)))
                    m_list.select_row(*row);
                break;
            }
    }
    m_rebuilding = false;

    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count();
    if (auto lg = log::get(log::Area::Tree))
        lg->info("rebuild: {} row(s) of {} node(s) in {:.2f} ms", m_row_ids.size(),
                 m_src ? m_src->count() : 0, ms);
}

void TreePane::append_rows(const core::NodeId& parent, int depth) {
    for (const auto& id : m_src->children(parent)) {
        const core::Node* n = m_src->find(id);
        if (!n) continue;                                  // index skew would be a model bug
        const bool has_children = !m_src->children(id).empty();
        m_list.append(*build_row(*n, depth, has_children));
        m_row_ids.push_back(id);
        if (has_children && !is_collapsed(id)) append_rows(id, depth + 1);
    }
}

// One row: twisty, title, and a marker for the two flags worth seeing at a
// glance. Row widgets take `unregistered` -- named for the Inspector, absent
// from the registry, because a rebuild would otherwise churn hundreds of
// entries through the live address book (CANON: the unregistered_t primitive).
Gtk::Widget* TreePane::build_row(const core::Node& n, int depth, bool has_children) {
    auto* box = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                "tree.row." + n.id,
                                                Gtk::Orientation::HORIZONTAL, 4);
    box->set_margin_start(6 + depth * 16);
    box->set_margin_end(6);
    box->set_margin_top(2);
    box->set_margin_bottom(2);

    // The twisty is built for EVERY row, children or not. A leaf's copy is
    // invisible and inert, which is what keeps its title on the same x as its
    // siblings' -- a hand-sized spacer does not match a GTK button's natural
    // width, and the resulting few-pixel drift is visible in a column of rows
    // and invisible to a compiler (s002: it shipped, and the screenshot caught
    // it).
    auto* twisty = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                      "tree.twisty." + n.id);
    twisty->set_has_frame(false);
    twisty->set_valign(Gtk::Align::CENTER);
    if (has_children) {
        twisty->set_icon_name(is_collapsed(n.id) ? "pan-end-symbolic" : "pan-down-symbolic");
        const core::NodeId id = n.id;
        twisty->signal_clicked().connect([this, id]() { toggle_collapsed(id); });
    } else {
        twisty->set_icon_name("pan-down-symbolic");   // for the size only
        twisty->set_opacity(0.0);
        twisty->set_sensitive(false);
        twisty->set_can_focus(false);
    }
    box->append(*twisty);

    // ── the todo half of a row ──────────────────────────────────────────────
    // A todo is a NODE (D2), so it is not a separate list living somewhere
    // else: it is this row, with a box on it. That is what "notes and todos
    // together" means at the surface, and it is why the tree can be the todo
    // list without becoming a second one.
    //
    // The box is built ONLY for a todo. Reserving the space on every note --
    // the way the twisty reserves its width -- would put an empty checkbox
    // column down a tree that is mostly notes, and quietly suggest that every
    // note is a task you have not done.
    if (n.task.is_task) {
        auto* tick = Gtk::make_managed<widgets::CheckButton>(widgets::unregistered,
                                                             "tree.done." + n.id);
        tick->set_valign(Gtk::Align::CENTER);
        tick->set_active(n.task.done);      // BEFORE the handler: set_active emits
        tick->set_can_focus(false);         // Space belongs to the list, not the box
        const core::NodeId id = n.id;
        tick->signal_toggled().connect([this, id, tick]() {
            if (m_rebuilding || !m_src) return;
            m_src->set_done(id, tick->get_active());
        });
        tick->set_tooltip_text(n.task.done ? "Done" : "Not done yet");
        box->append(*tick);
    }

    if (m_renaming == n.id) {
        // THE RENAME VARIANT. Same row, same geometry, an Entry where the Label
        // was -- so the title does not jump as you start typing, which is the
        // tell that an inline rename is a real inline rename and not a popup
        // wearing one's clothes.
        auto* entry = Gtk::make_managed<widgets::Entry>(widgets::unregistered,
                                                        "tree.rename." + n.id);
        entry->set_text(n.title);
        entry->set_hexpand(true);
        entry->set_has_frame(false);
        entry->select_region(0, -1);   // typing replaces; the common case

        // Enter commits. Escape abandons. Focus leaving commits too, because
        // clicking away from a rename you typed means you meant it -- losing
        // the edit there would be the app throwing away work to be tidy.
        entry->signal_activate().connect([this]() { end_rename(true); });

        auto key = Gtk::EventControllerKey::create();
        key->signal_key_pressed().connect(
            [this](guint keyval, guint, Gdk::ModifierType) {
                if (keyval == GDK_KEY_Escape) { end_rename(false); return true; }
                return false;
            }, false);
        entry->add_controller(key);

        auto focus = Gtk::EventControllerFocus::create();
        focus->signal_leave().connect([this]() { end_rename(true); });
        entry->add_controller(focus);

        m_rename_entry = entry;
        box->append(*entry);
    } else {
        auto* label = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                        "tree.title." + n.id);
        const std::string text = n.title.empty() ? "(untitled)" : n.title;
        // A finished todo is struck through and dimmed rather than removed: the
        // tree is the note tree, and a done task is still a note you may want
        // to read. Markup, so the escape is explicit -- a title containing an
        // ampersand would otherwise take the label out with it.
        if (n.task.is_task && n.task.done) {
            label->set_markup("<s>" + Glib::Markup::escape_text(text) + "</s>");
            label->add_css_class("dim-label");
        } else {
            label->set_text(text);
        }
        label->set_xalign(0.0f);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        // An ellipsised title is unreadable without this, and the pane is narrow
        // by default. The tooltip is the only place the full title survives.
        if (!n.title.empty()) label->set_tooltip_text(n.title);
        label->set_hexpand(true);
        box->append(*label);
    }

    // Flagged: "this one, today". It earns a marker in the tree because the
    // whole point of flagging is seeing it without opening anything.
    if (n.task.is_task && n.task.flagged && !n.task.done) {
        auto* star = Gtk::make_managed<widgets::Image>(widgets::unregistered,
                                                       "tree.flag." + n.id);
        star->set_from_icon_name("starred-symbolic");
        star->set_tooltip_text("Flagged");
        box->append(*star);
    }

    if (n.protect) {
        auto* lock = Gtk::make_managed<widgets::Image>(widgets::unregistered,
                                                       "tree.lock." + n.id);
        lock->set_from_icon_name("changes-prevent-symbolic");
        lock->set_tooltip_text("Protected: cannot be moved, edited or deleted");
        box->append(*lock);
    }

    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_name("tree.listrow." + n.id);
    row->set_child(*box);
    attach_row_dnd(*row, n.id);

    // Double-click to rename. Not attached while THIS row is the one being
    // renamed: a second click inside your own entry is placing a cursor, and
    // restarting the rename underneath it would eat the edit.
    if (m_renaming != n.id) {
        auto click = Gtk::GestureClick::create();
        click->set_button(GDK_BUTTON_PRIMARY);
        const core::NodeId id = n.id;
        click->signal_pressed().connect(
            [this, id](int n_press, double, double) {
                if (n_press == 2) begin_rename(id);
            });
        row->add_controller(click);
    }
    return row;
}

void TreePane::select(const core::NodeId& id) {
    m_selected = id;
    for (std::size_t i = 0; i < m_row_ids.size(); ++i)
        if (m_row_ids[i] == id) {
            if (auto* row = m_list.get_row_at_index(static_cast<int>(i))) {
                m_rebuilding = true;          // programmatic: not a user choice
                m_list.select_row(*row);
                m_rebuilding = false;
            }
            return;
        }
}

// ─────────────────────────────────────────────────────────────────────────────
// begin_rename / end_rename -- editing a title in the row it lives in.
//
// A protected node refuses, and refuses VISIBLY in the log rather than by
// doing nothing: "double-click does nothing on this row and I do not know why"
// is a worse experience than a refusal with a reason. Protection means
// read-only, and a title is part of the note.
// ─────────────────────────────────────────────────────────────────────────────
void TreePane::begin_rename(const core::NodeId& id) {
    if (!m_src || id.empty()) return;
    const core::Node* n = m_src->find(id);
    if (!n) return;
    if (n->protect) {
        if (auto lg = log::get(log::Area::Tree))
            lg->info("rename '{}': refused (protected)", id);
        return;
    }
    if (m_renaming == id) return;

    // Any rename already in flight commits first -- double-clicking a second
    // row while editing a first means you are done with the first.
    if (renaming()) end_rename(true);

    m_renaming = id;
    rebuild();                         // build_row emits the Entry for this id
    if (m_rename_entry) m_rename_entry->grab_focus();
}

void TreePane::end_rename(bool commit) {
    if (!renaming()) return;

    // Clear the state BEFORE anything that can re-enter. The focus-leave
    // handler fires during the rebuild that ends the rename, so a second
    // end_rename() arrives while the first is still running; without this it
    // recurses through rebuild() and destroys the entry it is reading from.
    const core::NodeId id = m_renaming;
    Gtk::Entry* entry = m_rename_entry;
    m_renaming.clear();
    m_rename_entry = nullptr;

    if (commit && entry && m_src) {
        const std::string title = std::string(entry->get_text());
        const core::Node* n = m_src->find(id);
        // Only write when it actually changed: an unchanged set_title would
        // still fire Change::Title and bump `modified`, so opening a rename and
        // pressing Escape-equivalent-by-clicking-away would silently mark the
        // note edited.
        if (n && n->title != title) {
            m_src->set_title(id, title);
            return;                    // the model's notify drives the rebuild
        }
    }
    rebuild();                         // nothing changed: put the label back
    select(id);
}

// ─────────────────────────────────────────────────────────────────────────────
// reveal -- make a node visible, then select it.
//
// Pole B makes this simple in a way Pole A would not: "visible" means "emitted
// by the last rebuild", and collapse is "don't emit the descendants". So
// revealing is un-collapsing the ancestors and rebuilding -- no path expansion
// API, no model-side row lookup, no waiting for a lazy child list to populate.
// ─────────────────────────────────────────────────────────────────────────────
void TreePane::reveal(const core::NodeId& id) {
    if (!m_src || id.empty() || !m_src->find(id)) return;

    bool opened = false;
    // Walk UP from the node, not down from the roots: the chain is at most the
    // depth of the tree, and a cycle is impossible by can_move()'s invariant.
    // The belt-and-braces hop limit is because this walks a data structure the
    // user drags around, and an infinite loop here would hang the app.
    core::NodeId cur = id;
    for (int hops = 0; hops < 1024; ++hops) {
        const core::Node* n = m_src->find(cur);
        if (!n || n->parent_id.empty()) break;
        if (m_collapsed.erase(n->parent_id)) opened = true;
        cur = n->parent_id;
    }
    if (opened) rebuild();
    select(id);
}

}  // namespace jot
