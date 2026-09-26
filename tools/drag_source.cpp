// tools/drag_source.cpp -- a window that drags ONE file, for testing drops
// in a sandbox with no file manager (s019). Under Xvfb:
//
//   ./build/jot_drag_source "/path/to/file.pdf" &
//   xdotool mousemove <src> mousedown 1 mousemove <jot's note> mouseup 1
//
// Hold `xdotool keydown ctrl shift` during the drag to test the link flip.
// Offers COPY and LINK, as Files does. Not installed; a test instrument.
#include <gtkmm.h>
static std::string g_path;
class W : public Gtk::Window {
public:
    W() {
        set_title("dragsrc"); set_default_size(220, 120);
        lbl.set_text("DRAG ME"); set_child(lbl);
        auto ds = Gtk::DragSource::create();
        ds->set_actions(Gdk::DragAction::COPY | Gdk::DragAction::LINK);
        ds->signal_prepare().connect([](double, double) {
            GFile* f = g_file_new_for_path(g_path.c_str());
            GSList* l = g_slist_append(nullptr, f);
            GdkFileList* fl = gdk_file_list_new_from_list(l);
            GValue v = G_VALUE_INIT;
            g_value_init(&v, GDK_TYPE_FILE_LIST);
            g_value_take_boxed(&v, fl);
            GdkContentProvider* cp = gdk_content_provider_new_for_value(&v);
            g_value_unset(&v); g_slist_free(l); g_object_unref(f);
            return Glib::wrap(cp);
        }, false);
        // What the TARGET told us: delete_data true means "it was moved,
        // delete your copy" -- the thing jot must never say (s019c).
        ds->signal_drag_end().connect([](const Glib::RefPtr<Gdk::Drag>& d, bool del) {
            g_print("drag-end: action=%d delete_data=%d\n",
                    static_cast<int>(d->get_selected_action()), del ? 1 : 0);
        });
        lbl.add_controller(ds);
    }
    Gtk::Label lbl;
};
int main(int argc, char** argv) {
    g_path = argv[1];
    auto app = Gtk::Application::create("test.dragsrc");
    return app->make_window_and_run<W>(1, argv);
}
