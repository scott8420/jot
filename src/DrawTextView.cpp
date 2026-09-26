#include "widgets/DrawTextView.hpp"

#include <string>

namespace jot::widgets {
namespace {
GtkTextViewClass* g_parent = nullptr;   // GtkTextView's own class, for the chain-up
}

DrawTextView::DrawTextView(std::string_view name)
    : Glib::ObjectBase("JotDrawTextView"),
      Glib::ExtraClassInit(&DrawTextView::class_init),
      Gtk::TextView() {
    set_name(std::string(name));
    registry::add(name, this);
}

DrawTextView::~DrawTextView() { registry::remove(this); }

void DrawTextView::class_init(void* g_class, void*) {
    g_parent = GTK_TEXT_VIEW_CLASS(g_type_class_peek_parent(g_class));
    GTK_TEXT_VIEW_CLASS(g_class)->snapshot_layer = &DrawTextView::snapshot_layer;
}

void DrawTextView::snapshot_layer(GtkTextView* view, GtkTextViewLayer layer, GtkSnapshot* snapshot) {
    if (g_parent && g_parent->snapshot_layer) g_parent->snapshot_layer(view, layer, snapshot);
    if (layer != GTK_TEXT_VIEW_LAYER_ABOVE_TEXT) return;
    auto* base = Glib::ObjectBase::_get_current_wrapper(reinterpret_cast<GObject*>(view));
    auto* self = dynamic_cast<DrawTextView*>(base);
    if (self && self->draw_above) self->draw_above(snapshot);
}

}  // namespace jot::widgets
