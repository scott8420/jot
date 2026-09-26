#include "TreePane.hpp"
#include "Log.hpp"

#include <gdkmm/contentprovider.h>
#include <gdkmm/display.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/droptarget.h>
#include <glibmm/main.h>

#include <gtk/gtk.h>

// TreePane_dnd.cpp -- DND. The drag source, the three-zone drop targets, the
// indicator, and the single place a drag reaches the model.
//
// This file is the one that matters. Notr stored a flat node set keyed by
// parent uuid -- the right shape -- and then its drop handler went through the
// tree widget's API instead: copy the node, append the copy, delete the
// original. It could not move a subtree at all, and it minted a new identity on
// every drag, which would have broken every inbound link the moment links
// existed.
//
// So the rule here is structural, not a matter of care: **nothing in this file
// touches a widget in response to a drop.** The drop resolves a zone to a
// (parent, index) pair, hands it to NodeSource::move(), and stops. The tree
// redraws because the model said it changed. There is no widget move to get
// wrong.
//
// The indicator is the exception that proves it: the CSS classes below are
// written DURING the drag, never after the drop, and they are cleared by the
// rebuild that the model triggers.

namespace jot {

namespace {

// The drag payload's type, named once so the source and the target cannot
// disagree about it -- a mismatch here is a drag that silently never drops.
GType payload_type() { return Glib::Value<Glib::ustring>::value_type(); }

// The indicator, as a stylesheet installed once per display. A heavy line on
// the row's top edge means "lands above", on the bottom edge "lands below", and
// a heavy stroke round the whole row means "lands inside" -- the same three
// signals Folio's binder draws, which is the vocabulary Scott already reads
// without being told.
void install_drop_css() {
    static bool done = false;
    if (done) return;
    auto display = Gdk::Display::get_default();
    if (!display) return;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        ".jot-drop-into  { outline: 2px solid @theme_selected_bg_color;"
        "                  outline-offset: -2px; border-radius: 6px; }"
        ".jot-drop-above { border-top: 3px solid @theme_selected_bg_color; }"
        ".jot-drop-below { border-bottom: 3px solid @theme_selected_bg_color; }");
    // The C call rather than Gtk::StyleContext::add_provider_for_display, which
    // is deprecated from gtkmm 4.12 -- the sandbox is 4.10 and Fedora is ahead
    // of it, and a deprecation warning on Scott's machine is noise I put there.
    gtk_style_context_add_provider_for_display(
        display->gobj(), GTK_STYLE_PROVIDER(css->gobj()),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    done = true;
}

}  // namespace

// ── geometry ────────────────────────────────────────────────────────────────
// Quarters rather than thirds: "inside" is the common case and deserves the
// bigger target, and the edges only need to be reachable, not comfortable.
TreePane::DropZone TreePane::zone_for(const Gtk::Widget& row, double y) const {
    const double h = row.get_height();
    if (h <= 0.0) return DropZone::Into;
    if (y < h * 0.25) return DropZone::Above;
    if (y > h * 0.75) return DropZone::Below;
    return DropZone::Into;
}

// Turn a zone on a target row into the pair the model speaks: a parent and a
// position among its children. An edge zone adopts the TARGET's parent -- that
// is what "above this row" means to the eye.
bool TreePane::resolve_drop(const core::NodeId& target, DropZone z,
                            core::NodeId& parent, int& index) const {
    if (!m_src) return false;
    if (target.empty()) { parent.clear(); index = -1; return true; }   // the list background

    if (z == DropZone::Into) { parent = target; index = -1; return true; }

    const core::Node* t = m_src->find(target);
    if (!t) return false;
    const int at = core::sibling_index(*m_src, target);
    if (at < 0) return false;
    parent = t->parent_id;
    index  = (z == DropZone::Above) ? at : at + 1;
    return true;
}

// ── the indicator ───────────────────────────────────────────────────────────
void TreePane::clear_drop_feedback() {
    if (!m_feedback_row) return;
    m_feedback_row->remove_css_class("jot-drop-into");
    m_feedback_row->remove_css_class("jot-drop-above");
    m_feedback_row->remove_css_class("jot-drop-below");
    m_feedback_row = nullptr;
}

void TreePane::show_drop_feedback(Gtk::Widget* row, DropZone z) {
    if (m_feedback_row != row) clear_drop_feedback();
    if (!row) return;
    m_feedback_row = row;
    row->remove_css_class("jot-drop-into");
    row->remove_css_class("jot-drop-above");
    row->remove_css_class("jot-drop-below");
    switch (z) {
        case DropZone::Above: row->add_css_class("jot-drop-above"); break;
        case DropZone::Below: row->add_css_class("jot-drop-below"); break;
        case DropZone::Into:  row->add_css_class("jot-drop-into");  break;
    }
}

// ── wiring ──────────────────────────────────────────────────────────────────
void TreePane::attach_row_dnd(Gtk::Widget& row, const core::NodeId& id) {
    install_drop_css();

    // ── source: this row offers its node id ────────────────────────────────
    auto source = Gtk::DragSource::create();
    source->set_actions(Gdk::DragAction::MOVE);
    source->signal_prepare().connect(
        [this, id](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
            m_dragging = id;          // so `motion` can judge legality live
            Glib::Value<Glib::ustring> v;
            v.init(payload_type());
            v.set(id);
            return Gdk::ContentProvider::create(v);
        },
        false);
    source->signal_drag_end().connect(
        [this](const Glib::RefPtr<Gdk::Drag>&, bool) {
            m_dragging.clear();
            clear_drop_feedback();
        });
    row.add_controller(source);

    // ── target: three zones ────────────────────────────────────────────────
    auto target = Gtk::DropTarget::create(payload_type(), Gdk::DragAction::MOVE);
    Gtk::Widget* row_ptr = &row;

    // motion runs on every pointer move over the row: it decides the zone, asks
    // the MODEL whether that drop is legal, and answers with an action. Refusing
    // here is what gives the user a no-drop cursor instead of a silent failure
    // at release time.
    target->signal_motion().connect(
        [this, row_ptr, id](double, double y) -> Gdk::DragAction {
            const DropZone z = zone_for(*row_ptr, y);
            core::NodeId parent;
            int index = -1;
            if (!resolve_drop(id, z, parent, index)) { clear_drop_feedback(); return {}; }
            if (!m_dragging.empty() &&
                !core::can_move(*m_src, m_dragging, parent, nullptr)) {
                clear_drop_feedback();
                return {};                      // no action == the refused cursor
            }
            show_drop_feedback(row_ptr, z);
            return Gdk::DragAction::MOVE;
        },
        false);

    target->signal_leave().connect([this]() { clear_drop_feedback(); });

    target->signal_drop().connect(
        [this, row_ptr, id](const Glib::ValueBase& value, double, double y) {
            clear_drop_feedback();
            if (!G_VALUE_HOLDS(value.gobj(), payload_type())) return false;
            Glib::Value<Glib::ustring> v;
            v.init(value.gobj());
            return drop_at(v.get(), id, zone_for(*row_ptr, y));
        },
        false);
    row.add_controller(target);
}

// The list's own background: below the last row is "no parent", i.e. promote to
// top level. Rows sit on top of it and have their own targets, so this only
// fires where there is no row.
void TreePane::attach_root_drop() {
    auto target = Gtk::DropTarget::create(payload_type(), Gdk::DragAction::MOVE);
    target->signal_drop().connect(
        [this](const Glib::ValueBase& value, double, double) {
            clear_drop_feedback();
            if (!G_VALUE_HOLDS(value.gobj(), payload_type())) return false;
            Glib::Value<Glib::ustring> v;
            v.init(value.gobj());
            return drop_at(v.get(), "", DropZone::Into);
        },
        false);
    m_list.add_controller(target);
}

// s021b -- files from OUTSIDE (a .md from Files). ONE target on the list, in
// the CAPTURE phase: a drag from Files also offers its uri as a STRING, and the
// rows' own targets take strings (a node id), so heard second, a dropped file
// would arrive at a row as a "node id" that moves nothing. Heard first, it is
// a file list. Whatever row is under the pointer is the parent -- Into only;
// "above / below" between siblings is a refinement nobody has asked for.
core::NodeId TreePane::row_id_at(double y) const {
    auto* row = const_cast<widgets::ListBox&>(m_list).get_row_at_y(static_cast<int>(y));
    if (!row) return {};
    const int i = row->get_index();
    if (i < 0 || i >= static_cast<int>(m_row_ids.size())) return {};
    return m_row_ids[static_cast<std::size_t>(i)];
}

void TreePane::attach_file_drop() {
    auto target = Gtk::DropTarget::create(GDK_TYPE_FILE_LIST,
                                          Gdk::DragAction::COPY | Gdk::DragAction::MOVE);
    target->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    target->signal_motion().connect(
        [this](double, double y) -> Gdk::DragAction {
            Gtk::Widget* row = nullptr;
            const core::NodeId id = row_id_at(y);
            if (!id.empty()) row = m_list.get_row_at_y(static_cast<int>(y));
            if (row) show_drop_feedback(row, DropZone::Into);
            else     clear_drop_feedback();
            return Gdk::DragAction::COPY;
        },
        false);
    target->signal_leave().connect([this]() { clear_drop_feedback(); });
    target->signal_drop().connect(
        [this, target](const Glib::ValueBase& value, double, double y) {
            clear_drop_feedback();
            auto* list = static_cast<GdkFileList*>(g_value_get_boxed(value.gobj()));
            if (!list) return false;
            std::vector<std::string> paths;
            GSList* files = gdk_file_list_get_files(list);
            for (GSList* l = files; l; l = l->next) {
                char* p = g_file_get_path(G_FILE(l->data));
                if (p) paths.emplace_back(p);
                g_free(p);
            }
            g_slist_free(files);
            if (paths.empty()) return false;
            // A Shift-drag arrives as MOVE (s019c). Never FINISH it as one:
            // narrowed to COPY for this drop, so Files deletes nothing.
            target->set_actions(Gdk::DragAction::COPY);
            Glib::signal_idle().connect_once([target]() {
                target->set_actions(Gdk::DragAction::COPY | Gdk::DragAction::MOVE);
            });
            const core::NodeId parent = row_id_at(y);
            if (auto lg = log::get(log::Area::Tree))
                lg->info("files dropped on the tree: {} under '{}'", paths.size(),
                         parent.empty() ? "(top level)" : parent);
            m_sig_files.emit(paths, parent);
            return true;
        },
        false);
    m_list.add_controller(target);
}

// The whole reparent. A zone in, a (parent, index) out, one model verb, no
// widget work.
bool TreePane::drop_at(const core::NodeId& dragged, const core::NodeId& target,
                       DropZone z) {
    if (!m_src || dragged.empty()) return false;

    core::NodeId parent;
    int index = -1;
    if (!resolve_drop(target, z, parent, index)) return false;

    const char* zone_name = z == DropZone::Above ? "above"
                          : z == DropZone::Below ? "below" : "into";
    const std::string where = target.empty() ? "(top level)" : target;

    // Ask before doing, so the refusal can say why. can_move() is the model's
    // rule (cycles, protection), not this file's -- the surface must not grow a
    // second, drifting copy of it.
    std::string why;
    if (!core::can_move(*m_src, dragged, parent, &why)) {
        if (auto lg = log::get(log::Area::Tree))
            lg->info("drop '{}' {} '{}': refused ({})", dragged, zone_name, where, why);
        return false;
    }

    const bool ok = m_src->move(dragged, parent, index);
    if (auto lg = log::get(log::Area::Tree))
        lg->info("drop '{}' {} '{}' -> parent '{}' index {}: {}", dragged, zone_name,
                 where, parent.empty() ? "(top level)" : parent, index,
                 ok ? "moved" : "no change");

    // A dropped node should be the one you are looking at. Selection is the
    // pane's own state, not the model's, so setting it here is not a widget
    // rearrangement -- the rebuild still comes from the model's notification.
    if (ok) m_selected = dragged;
    return ok;
}

}  // namespace jot
