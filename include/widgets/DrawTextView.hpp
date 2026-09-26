#pragma once
#include "Registry.hpp"

#include <glibmm/extraclassinit.h>
#include <gtkmm/textview.h>
#include <functional>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
// DrawTextView (s023) -- a Gtk::TextView you can DRAW ON, above its text, in
// buffer coordinates.
//
// Live Preview draws a bullet, a box, a rule, a picture and a code block's
// Copy button over the note without putting a single character into the
// buffer (the buffer is the file, byte for byte). GtkTextView has exactly the
// hook for that -- the `snapshot_layer` class vfunc, called before and after
// the text with the snapshot already in buffer space, so what is drawn
// scrolls and re-wraps with the text for free. gtkmm 4.10 does not wrap it,
// so this class sets it at class-init time through Glib::ExtraClassInit and
// forwards to a std::function.
//
// Named and registered by hand rather than through Named<W>: ExtraClassInit
// needs a custom GType, which needs Glib::ObjectBase's name constructor, and
// ObjectBase is a VIRTUAL base -- only the most-derived class gets to
// construct it. Wrapped in Named<>, this class's custom type would silently
// never exist and the hook would never be called.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::widgets {

class DrawTextView : public Glib::ExtraClassInit, public Gtk::TextView {
public:
    explicit DrawTextView(std::string_view name);
    ~DrawTextView() override;

    // Called after the text is drawn. `snapshot` is a GtkSnapshot* in buffer
    // coordinates.
    std::function<void(GtkSnapshot*)> draw_above;

private:
    static void class_init(void* g_class, void* class_data);
    static void snapshot_layer(GtkTextView* view, GtkTextViewLayer layer, GtkSnapshot* snapshot);
};

}  // namespace jot::widgets
