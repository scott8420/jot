#pragma once
// GraphPane -- the graph view (s071f). Scott: "one thing jot doesn't do is
// show a cluster view like obsidian"; "right-click a context on a bubble and
// say center on here after the default build".
//
// The centre's third face (the note, the timeline, the graph): a head --
// title, what it adds up to, Show Everything when centred, Group ▾, Show
// (the funnel), find, ? -- over a canvas of bubbles. core/Graph decides what
// is drawn and how it settles; this draws it and takes the hand:
//
//   click        pick (Note details follows)      double-click  open the note
//   drag bubble  move it; it stays (pinned)       drag space    pan
//   scroll       zoom at the pointer              right-click   Open Note /
//   Esc          Show Everything, then back to the note           Center Here / Pin
#include "core/Graph.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/drawingarea.h>
#include <gtkmm/popovermenu.h>
#include <giomm/simpleactiongroup.h>
#include <sigc++/signal.h>

#include <set>
#include <string>

namespace jot {

class GraphCanvas : public Gtk::DrawingArea {
public:
    struct Box { double x0 = 0, y0 = 0, x1 = 0, y1 = 0; };   // world coordinates
    GraphCanvas();

    // Keeps bubbles where they were; `settle` runs the layout to rest first
    // (Group or Show changed: everything moves, so frame it settled).
    void set_graph(core::Graph g, bool settle = false);
    const core::Graph& graph() const { return m_g; }
    void set_current(const core::NodeId& id);
    void set_matches(const std::set<int>& m);   // empty = no find
    void center_on(int i);                      // -1 = Show Everything
    int  centered() const { return m_center; }
    void fit();                                 // frame what is on show
    void zoom_at(double px, double py, double factor);
    void zoom_step(double factor) { zoom_at(get_width() / 2.0, get_height() / 2.0, factor); }

    sigc::signal<void(core::NodeId)>& signal_pick() { return m_sig_pick; }
    sigc::signal<void(core::NodeId)>& signal_open() { return m_sig_open; }
    sigc::signal<void()>&              signal_close() { return m_sig_close; }
    sigc::signal<void(int)>&           signal_centered() { return m_sig_centered; }

private:
    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    int  hit(double sx, double sy) const;
    void to_world(double sx, double sy, double& wx, double& wy) const;
    void start_ticking(int budget);
    bool on_tick(const Glib::RefPtr<Gdk::FrameClock>&);
    void show_menu(int i, double x, double y);
    // s071g: the corner thumbnail, as on the timeline -- every bubble small,
    // the part on screen framed; press or drag in it to go there.
    Box  world_box() const;                     // every bubble, with room
    bool map_shown() const;                     // only when some of it is off screen
    bool in_map(double x, double y) const;
    void map_rect(double& x, double& y, double& w, double& h) const;
    void map_to_view(double x, double y);       // centre the view on that point of the map
    void draw_map(const Cairo::RefPtr<Cairo::Context>& cr);
    bool m_map_drag = false;

    core::Graph                 m_g;
    std::vector<core::GraphPos> m_pos;
    std::set<int>               m_focus;      // Center Here: what is gathered
    int                         m_center = -1;
    std::set<int>               m_matches;
    core::NodeId                m_current;
    int                         m_hover = -1;
    double m_zoom = 1.0, m_ox = 0, m_oy = 0;  // screen = world * zoom + offset
    bool   m_fitted = false;

    int    m_drag_node = -1;                  // a bubble in the hand
    bool   m_panning = false;
    bool   m_dragged = false;
    double m_drag_x = 0, m_drag_y = 0, m_drag_ox = 0, m_drag_oy = 0;

    guint m_tick = 0;
    int   m_budget = 0;                       // frames left to animate

    Gtk::PopoverMenu                     m_menu;
    Glib::RefPtr<Gio::SimpleActionGroup> m_actions;
    int                                  m_menu_node = -1;

    sigc::signal<void(core::NodeId)> m_sig_pick, m_sig_open;
    sigc::signal<void()>              m_sig_close;
    sigc::signal<void(int)>           m_sig_centered;
};

class GraphPane : public widgets::Box {
public:
    explicit GraphPane(std::string_view name);

    void set_source(core::NodeSource* src);
    void refresh();                           // re-read the model; bubbles stay put
    void opened();                            // shown: re-read; the find takes the keys
    void focus_find();
    // s071h: Ctrl+= / Ctrl+- / Ctrl+0 -- jot's zoom keys -- zoom the graph
    // while it shows (Scott: "i was using ctrl+ -/+").
    void zoom_in()    { m_canvas.zoom_step(1.25); }
    void zoom_out()   { m_canvas.zoom_step(1 / 1.25); }
    void zoom_reset() { m_canvas.fit(); }
    void set_current(const core::NodeId& id);

    // s068's shape: as it was left. set_view applies quietly; signal_view
    // fires when the reader changes Group or Show.
    void set_view(const std::string& group, const std::string& show);
    sigc::signal<void(std::string, std::string)>& signal_view() { return m_sig_view; }

    sigc::signal<void(core::NodeId)>& signal_pick() { return m_canvas.signal_pick(); }
    sigc::signal<void(core::NodeId)>& signal_open() { return m_canvas.signal_open(); }
    sigc::signal<void()>&              signal_close() { return m_canvas.signal_close(); }

    const core::Graph& graph() const { return m_canvas.graph(); }   // for the trace channel

private:
    void rebuild(bool settle = false);
    void run_find();
    void say_summary();
    void emit_view();

    core::NodeSource* m_src = nullptr;
    core::GraphShow   m_show;
    core::GraphGroup  m_group = core::GraphGroup::None;
    bool              m_applying = false;

    widgets::Box             m_head;
    widgets::Label           m_title;
    widgets::Label           m_summary;
    widgets::Button          m_everything;
    widgets::MenuButton      m_group_btn;
    widgets::MenuButton      m_show_btn;
    widgets::SearchEntry     m_find;
    widgets::Label           m_find_count;
    widgets::CheckButton     m_g_none, m_g_tag, m_g_place, m_g_project;
    widgets::CheckButton     m_s_links, m_s_tree, m_s_tags, m_s_done, m_s_notes;
    GraphCanvas              m_canvas;

    sigc::signal<void(std::string, std::string)> m_sig_view;
};

}  // namespace jot
