#include "GraphPane.hpp"

#include "Appearance.hpp"
#include "Log.hpp"
#include "core/Search.hpp"

#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/gesturedrag.h>
#include <giomm/menu.h>

#include <algorithm>
#include <cmath>
#include <ctime>

// GraphPane.cpp -- see the header. Every decision about WHAT is drawn and
// where it settles is core/Graph's; this file paints it and takes the hand.

namespace jot {

namespace {

std::int64_t now_s() { return static_cast<std::int64_t>(std::time(nullptr)); }

Gdk::RGBA rgba(const std::string& css) {
    Gdk::RGBA c;
    if (!c.set(css)) c.set_rgba(0.21, 0.52, 0.89, 1.0);
    return c;
}

void source(const Cairo::RefPtr<Cairo::Context>& cr, const Gdk::RGBA& c, double alpha = 1.0) {
    cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), c.get_alpha() * alpha);
}

void log_info(const std::string& s) {
    if (auto lg = log::get(log::Area::Shell)) lg->info("graph: {}", s);
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// GraphCanvas
// ═════════════════════════════════════════════════════════════════════════════
GraphCanvas::GraphCanvas() {
    set_name("shell.graph.canvas");
    set_hexpand(true);
    set_vexpand(true);
    set_focusable(true);
    set_draw_func(sigc::mem_fun(*this, &GraphCanvas::on_draw));

    // ── the hand ──────────────────────────────────────────────────────────
    auto drag = Gtk::GestureDrag::create();
    drag->set_button(GDK_BUTTON_PRIMARY);
    drag->signal_drag_begin().connect([this](double x, double y) {
        grab_focus();
        m_map_drag = map_shown() && in_map(x, y);   // s071g: the thumbnail takes it
        if (m_map_drag) {
            m_drag_node = -1;
            m_panning = false;
            m_dragged = true;   // never a click on what lies under the map
            m_drag_x = x;
            m_drag_y = y;
            map_to_view(x, y);
            return;
        }
        m_drag_node = hit(x, y);
        m_panning = m_drag_node < 0;
        m_dragged = false;
        m_drag_x = x;
        m_drag_y = y;
        m_drag_ox = m_ox;
        m_drag_oy = m_oy;
    });
    drag->signal_drag_update().connect([this](double dx, double dy) {
        if (m_map_drag) { map_to_view(m_drag_x + dx, m_drag_y + dy); return; }
        if (std::abs(dx) + std::abs(dy) > 3) m_dragged = true;
        if (!m_dragged) return;
        if (m_drag_node >= 0) {
            double wx, wy;
            to_world(m_drag_x + dx, m_drag_y + dy, wx, wy);
            auto& p = m_pos[static_cast<std::size_t>(m_drag_node)];
            p.x = wx;
            p.y = wy;
            p.pinned = true;   // put somewhere, it stays (Pin / Unpin lets it go)
            start_ticking(40);
        } else if (m_panning) {
            m_ox = m_drag_ox + dx;
            m_oy = m_drag_oy + dy;
        }
        queue_draw();
    });
    drag->signal_drag_end().connect([this](double, double) {
        if (m_dragged && m_drag_node >= 0)
            log_info("pinned '" + m_g.nodes[static_cast<std::size_t>(m_drag_node)].title + "'");
        m_drag_node = -1;
        m_panning = false;
        m_map_drag = false;
    });
    add_controller(drag);

    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_released().connect([this](int n, double x, double y) {
        if (m_dragged) return;
        const int i = hit(x, y);
        if (i < 0) return;
        const auto& id = m_g.nodes[static_cast<std::size_t>(i)].id;
        if (n >= 2) m_sig_open.emit(id);
        else m_sig_pick.emit(id);
    });
    add_controller(click);

    auto rclick = Gtk::GestureClick::create();
    rclick->set_button(GDK_BUTTON_SECONDARY);
    rclick->signal_pressed().connect([this](int, double x, double y) {
        const int i = hit(x, y);
        if (i >= 0) show_menu(i, x, y);
    });
    add_controller(rclick);

    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double y) {
        const int i = hit(x, y);
        if (i != m_hover) {
            m_hover = i;
            set_tooltip_text(i >= 0 ? m_g.nodes[static_cast<std::size_t>(i)].title : "");
            queue_draw();
        }
    });
    motion->signal_leave().connect([this]() {
        if (m_hover >= 0) { m_hover = -1; queue_draw(); }
    });
    add_controller(motion);

    // s071g (Scott: "allow ... scrolling"): the wheel and the touchpad move
    // the view, as on the timeline; Ctrl+scroll zooms at the pointer.
    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
    scroll->signal_scroll().connect([this, motion, scroll](double dx, double dy) {
        const auto mods = scroll->get_current_event_state();
        if ((mods & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK) {
            double px = get_width() / 2.0, py = get_height() / 2.0;
            if (auto ev = motion->get_current_event()) ev->get_position(px, py);
            zoom_at(px, py, dy > 0 ? 1 / 1.15 : (dy < 0 ? 1.15 : 1.0));
            return true;
        }
        const bool smooth = scroll->get_unit() == Gdk::ScrollUnit::SURFACE;
        const double step = smooth ? 1.0 : 48.0;
        if ((mods & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK && dx == 0) { dx = dy; dy = 0; }
        m_ox -= dx * step;
        m_oy -= dy * step;
        queue_draw();
        return true;
    }, false);
    add_controller(scroll);

    auto key = Gtk::EventControllerKey::create();
    key->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType) {
        if (kv == GDK_KEY_Escape) {
            if (m_center >= 0) center_on(-1);
            else m_sig_close.emit();
            return true;
        }
        if (kv == GDK_KEY_Home) { fit(); return true; }
        if (kv == GDK_KEY_plus || kv == GDK_KEY_equal || kv == GDK_KEY_KP_Add) {
            zoom_at(get_width() / 2.0, get_height() / 2.0, 1.2); return true;
        }
        if (kv == GDK_KEY_minus || kv == GDK_KEY_KP_Subtract) {
            zoom_at(get_width() / 2.0, get_height() / 2.0, 1 / 1.2); return true;
        }
        return false;
    }, false);
    add_controller(key);

    // ── the bubble's menu ─────────────────────────────────────────────────
    m_actions = Gio::SimpleActionGroup::create();
    m_actions->add_action("open", [this]() {
        if (m_menu_node >= 0) m_sig_open.emit(m_g.nodes[static_cast<std::size_t>(m_menu_node)].id);
    });
    m_actions->add_action("center", [this]() { if (m_menu_node >= 0) center_on(m_menu_node); });
    m_actions->add_action("pin", [this]() {
        if (m_menu_node < 0) return;
        auto& p = m_pos[static_cast<std::size_t>(m_menu_node)];
        p.pinned = !p.pinned;
        log_info(std::string(p.pinned ? "pinned '" : "unpinned '") +
                 m_g.nodes[static_cast<std::size_t>(m_menu_node)].title + "'");
        start_ticking(90);
        queue_draw();
    });
    m_actions->add_action("everything", [this]() { center_on(-1); });
    insert_action_group("graph", m_actions);
    m_menu.set_parent(*this);
    m_menu.set_has_arrow(true);
}

void GraphCanvas::show_menu(int i, double x, double y) {
    m_menu_node = i;
    auto model = Gio::Menu::create();
    auto top = Gio::Menu::create();
    top->append("Open Note", "graph.open");
    top->append("Center Here", "graph.center");
    model->append_section(top);
    auto more = Gio::Menu::create();
    more->append(m_pos[static_cast<std::size_t>(i)].pinned ? "Unpin" : "Pin Here", "graph.pin");
    if (m_center >= 0) more->append("Show Everything", "graph.everything");
    model->append_section(more);
    m_menu.set_menu_model(model);
    Gdk::Rectangle r(static_cast<int>(x), static_cast<int>(y), 1, 1);
    m_menu.set_pointing_to(r);
    m_menu.popup();
}

// A group's halo: the middle of its shown bubbles and a radius round them all.
static bool halo_of(const core::Graph& g, const std::vector<core::GraphPos>& pos, const std::set<int>& focus,
                    const std::string& name, double& cx, double& cy, double& r) {
    cx = cy = r = 0;
    int n = 0;
    for (std::size_t i = 0; i < g.nodes.size(); ++i)
        if (g.nodes[i].group == name && (focus.empty() || focus.count(static_cast<int>(i)))) {
            cx += pos[i].x; cy += pos[i].y; ++n;
        }
    if (n == 0) return false;
    cx /= n; cy /= n;
    for (std::size_t i = 0; i < g.nodes.size(); ++i)
        if (g.nodes[i].group == name && (focus.empty() || focus.count(static_cast<int>(i))))
            r = std::max(r, std::hypot(pos[i].x - cx, pos[i].y - cy) + core::graph_radius(g.nodes[i]));
    r += 18;
    return true;
}

void GraphCanvas::to_world(double sx, double sy, double& wx, double& wy) const {
    wx = (sx - m_ox) / m_zoom;
    wy = (sy - m_oy) / m_zoom;
}

int GraphCanvas::hit(double sx, double sy) const {
    if (map_shown() && in_map(sx, sy)) return -1;   // s071g: the thumbnail is on top
    double wx, wy;
    to_world(sx, sy, wx, wy);
    int best = -1;
    double bd = 1e18;
    for (std::size_t i = 0; i < m_g.nodes.size() && i < m_pos.size(); ++i) {
        const double r = core::graph_radius(m_g.nodes[i]) + 4.0 / m_zoom;
        const double d = std::hypot(m_pos[i].x - wx, m_pos[i].y - wy);
        if (d <= r && d < bd) { bd = d; best = static_cast<int>(i); }
    }
    return best;
}

void GraphCanvas::set_graph(core::Graph g, bool settle) {
    const core::Graph old = std::move(m_g);
    const auto old_pos = m_pos;
    const core::NodeId center_id = m_center >= 0 && m_center < static_cast<int>(old.nodes.size())
                                       ? old.nodes[static_cast<std::size_t>(m_center)].id : core::NodeId{};
    m_g = std::move(g);
    m_pos = core::graph_seed(m_g, &old, &old_pos);
    m_hover = -1;
    m_center = center_id.empty() ? -1 : m_g.index_of(center_id);
    m_focus = m_center >= 0 ? core::graph_around(m_g, m_center, 2) : std::set<int>{};
    // New bubbles settle at once, off screen, before the first frame: a few
    // hundred steps for a small folder, fewer for a big one (each is n²).
    bool any_new = old.nodes.empty();
    for (const auto& n : m_g.nodes) if (old.index_of(n.id) < 0) { any_new = true; break; }
    if (any_new || settle) {
        const double n = std::max<double>(1, static_cast<double>(m_g.nodes.size()));
        const int steps = std::clamp(static_cast<int>(40'000'000.0 / (n * n)), 25, 300);
        for (int s = 0; s < steps; ++s) core::graph_step(m_g, m_pos, m_focus);
    }
    if (!m_fitted && !m_g.nodes.empty()) fit();
    start_ticking(30);
    queue_draw();
}

void GraphCanvas::set_current(const core::NodeId& id) {
    if (m_current == id) return;
    m_current = id;
    queue_draw();
}

void GraphCanvas::set_matches(const std::set<int>& m) {
    m_matches = m;
    queue_draw();
}

void GraphCanvas::center_on(int i) {
    m_center = i;
    m_focus = i >= 0 ? core::graph_around(m_g, i, 2) : std::set<int>{};
    if (i >= 0) {
        // Gather: the centre to the middle of what is gathered, then let the
        // gathered bubbles settle around it.
        double cx = 0, cy = 0;
        for (int k : m_focus) { cx += m_pos[static_cast<std::size_t>(k)].x; cy += m_pos[static_cast<std::size_t>(k)].y; }
        cx /= static_cast<double>(m_focus.size());
        cy /= static_cast<double>(m_focus.size());
        auto& c = m_pos[static_cast<std::size_t>(i)];
        for (int k : m_focus) {
            auto& p = m_pos[static_cast<std::size_t>(k)];
            if (p.pinned || k == i) continue;
            p.x = cx + (p.x - cx) * 0.5;
            p.y = cy + (p.y - cy) * 0.5;
        }
        if (!c.pinned) { c.x = cx; c.y = cy; }
        for (int s = 0; s < 60; ++s) core::graph_step(m_g, m_pos, m_focus);
        log_info("centered on '" + m_g.nodes[static_cast<std::size_t>(i)].title + "' -- " +
                 std::to_string(m_focus.size()) + " of " + std::to_string(m_g.nodes.size()));
    } else {
        log_info("everything -- " + std::to_string(m_g.nodes.size()) + " bubble(s)");
    }
    fit();
    start_ticking(60);
    m_sig_centered.emit(i);
}

void GraphCanvas::fit() {
    // Not laid out yet (the view is hidden): frame it on the first real draw.
    if (get_width() <= 1 || get_height() <= 1) { m_fitted = false; return; }
    m_fitted = true;
    const int w = get_width(), h = get_height();
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    bool any = false;
    for (std::size_t i = 0; i < m_pos.size(); ++i) {
        if (!m_focus.empty() && !m_focus.count(static_cast<int>(i))) continue;
        // Room for the name under it -- and, grouped, for the halo and its name.
        const double r = core::graph_radius(m_g.nodes[i]) + 30;
        x0 = std::min(x0, m_pos[i].x - r); x1 = std::max(x1, m_pos[i].x + r);
        y0 = std::min(y0, m_pos[i].y - r); y1 = std::max(y1, m_pos[i].y + r);
        any = true;
    }
    for (const auto& name : m_g.groups) {   // the halos, and the names above them
        double cx, cy, r;
        if (!halo_of(m_g, m_pos, m_focus, name, cx, cy, r)) continue;
        x0 = std::min(x0, cx - r - 10); x1 = std::max(x1, cx + r + 10);
        y0 = std::min(y0, cy - r - 34); y1 = std::max(y1, cy + r + 10);
    }
    if (!any) { m_zoom = 1; m_ox = w / 2.0; m_oy = h / 2.0; queue_draw(); return; }
    m_zoom = std::clamp(std::min(w / (x1 - x0), h / (y1 - y0)), 0.15, 1.6);
    m_ox = w / 2.0 - (x0 + x1) / 2 * m_zoom;
    m_oy = h / 2.0 - (y0 + y1) / 2 * m_zoom;
    queue_draw();
}

void GraphCanvas::start_ticking(int budget) {
    m_budget = std::max(m_budget, budget);
    if (m_tick) return;
    m_tick = add_tick_callback(sigc::mem_fun(*this, &GraphCanvas::on_tick));
}

bool GraphCanvas::on_tick(const Glib::RefPtr<Gdk::FrameClock>&) {
    const double moved = core::graph_step(m_g, m_pos, m_focus);
    queue_draw();
    if (--m_budget <= 0 || moved < 0.3) {
        m_tick = 0;
        m_budget = 0;
        return false;
    }
    return true;
}

void GraphCanvas::on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    if (!m_fitted && !m_g.nodes.empty()) fit();
    GdkRGBA fgc;
    gtk_widget_get_color(GTK_WIDGET(gobj()), &fgc);
    const Gdk::RGBA fg(fgc.red, fgc.green, fgc.blue, 1.0);
    const Gdk::RGBA accent = rgba(appearance::accent_in_force());
    const bool dark = appearance::is_dark();
    const auto sc = appearance::state_colours();
    (void)w;
    (void)h;

    if (m_g.nodes.empty()) {
        auto lay = create_pango_layout("No notes to draw yet.");
        int lw, lh;
        lay->get_pixel_size(lw, lh);
        source(cr, fg, 0.5);
        cr->move_to((w - lw) / 2.0, (h - lh) / 2.0);
        lay->show_in_cairo_context(cr);
        return;
    }

    // What is lit: the hovered bubble and its neighbours; else the find's
    // matches; Center Here's gathering is lit and the rest faded.
    std::set<int> near;
    if (m_hover >= 0) {
        near.insert(m_hover);
        for (const auto& e : m_g.edges) {
            if (e.a == m_hover) near.insert(e.b);
            if (e.b == m_hover) near.insert(e.a);
        }
    }
    auto shown = [&](int i) { return m_focus.empty() || m_focus.count(i); };
    auto alpha_of = [&](int i) {
        if (!shown(i)) return 0.10;
        if (!near.empty()) return near.count(i) ? 1.0 : 0.25;
        if (!m_matches.empty()) return m_matches.count(i) ? 1.0 : 0.20;
        return 1.0;
    };
    auto colour_of = [&](const core::GraphNode& n) -> std::pair<Gdk::RGBA, double> {
        if (!n.todo) return {fg, n.project ? 0.75 : 0.45};
        switch (n.state) {
            case core::RowState::Overdue:   return {rgba(sc.late), 1.0};
            case core::RowState::DueToday:  return {rgba(sc.today), 1.0};
            case core::RowState::Flagged:   return {rgba(sc.flagged), 1.0};
            case core::RowState::Available: return {rgba(sc.available), 1.0};
            case core::RowState::Done:      return {rgba(sc.done), 1.0};
            default:                        return {fg, 0.40};
        }
    };

    cr->save();
    cr->translate(m_ox, m_oy);
    cr->scale(m_zoom, m_zoom);

    // Group halos: a soft disc round each clump, its name above it.
    if (!m_g.groups.empty()) {
        for (const auto& name : m_g.groups) {
            double cx, cy, r;
            if (!halo_of(m_g, m_pos, m_focus, name, cx, cy, r)) continue;
            source(cr, accent, dark ? 0.10 : 0.07);
            cr->begin_new_path();   // after the last label's text: no line to here
            cr->arc(cx, cy, r, 0, 2 * M_PI);
            cr->fill_preserve();
            source(cr, accent, 0.30);
            cr->set_line_width(1.0 / m_zoom);
            cr->stroke();
            auto lay = create_pango_layout(name);
            auto f = get_pango_context()->get_font_description();
            f.set_weight(Pango::Weight::BOLD);
            lay->set_font_description(f);
            int lw, lh;
            lay->get_pixel_size(lw, lh);
            cr->save();
            cr->translate(cx, cy - r - 4);
            cr->scale(1 / m_zoom, 1 / m_zoom);
            source(cr, accent, 0.9);
            cr->move_to(-lw / 2.0, -lh);
            lay->show_in_cairo_context(cr);
            cr->restore();
        }
    }

    // Lines.
    for (const auto& e : m_g.edges) {
        const auto& a = m_pos[static_cast<std::size_t>(e.a)];
        const auto& b = m_pos[static_cast<std::size_t>(e.b)];
        const double al = std::min(alpha_of(e.a), alpha_of(e.b));
        const bool lit = !near.empty() && near.count(e.a) && near.count(e.b) && (e.a == m_hover || e.b == m_hover);
        std::vector<double> dash;
        if (e.kind == core::GraphEdgeKind::Link) source(cr, lit ? accent : fg, (lit ? 0.9 : 0.38) * al);
        else if (e.kind == core::GraphEdgeKind::Tree) source(cr, lit ? accent : fg, (lit ? 0.8 : 0.20) * al);
        else { source(cr, lit ? accent : fg, (lit ? 0.7 : 0.18) * al); dash = {4 / m_zoom, 4 / m_zoom}; }
        cr->set_dash(dash, 0);
        cr->set_line_width((lit ? 1.8 : (e.kind == core::GraphEdgeKind::Link ? 1.3 : 1.0)) / m_zoom);
        cr->move_to(a.x, a.y);
        cr->line_to(b.x, b.y);
        cr->stroke();
    }
    cr->set_dash(std::vector<double>{}, 0);

    // Bubbles, then their names.
    const int n = static_cast<int>(m_g.nodes.size());
    for (int i = 0; i < n; ++i) {
        const auto& gn = m_g.nodes[static_cast<std::size_t>(i)];
        const auto& p = m_pos[static_cast<std::size_t>(i)];
        const double r = core::graph_radius(gn);
        const auto [c, ca] = colour_of(gn);
        const double al = alpha_of(i);
        source(cr, c, ca * al);
        cr->begin_new_path();
        cr->arc(p.x, p.y, r, 0, 2 * M_PI);
        cr->fill();
        if (gn.project) {   // a project: a ring round it
            source(cr, c, 0.9 * al);
            cr->set_line_width(1.6 / m_zoom);
            cr->arc(p.x, p.y, r + 3, 0, 2 * M_PI);
            cr->stroke();
        }
        if (gn.id == m_current || i == m_center) {   // the note on show; the centre
            source(cr, accent, al);
            cr->set_line_width(2.5 / m_zoom);
            cr->arc(p.x, p.y, r + (gn.project ? 6 : 4), 0, 2 * M_PI);
            cr->stroke();
        }
        if (p.pinned) {   // a pin: a small dot on top
            source(cr, dark ? Gdk::RGBA("white") : Gdk::RGBA("black"), 0.7 * al);
            cr->arc(p.x, p.y - r - 3, 1.8 / m_zoom + 0.8, 0, 2 * M_PI);
            cr->fill();
        }
    }
    auto font = get_pango_context()->get_font_description();
    for (int i = 0; i < n; ++i) {
        const auto& gn = m_g.nodes[static_cast<std::size_t>(i)];
        const bool important = i == m_hover || near.count(i) || gn.id == m_current || i == m_center ||
                               m_matches.count(i) || (m_focus.count(i) && m_zoom > 0.5);
        if (!important && (m_zoom < 0.45 && gn.degree < 4)) continue;
        if (!important && m_zoom < 0.25) continue;
        const double al = alpha_of(i);
        if (al < 0.2) continue;
        const auto& p = m_pos[static_cast<std::size_t>(i)];
        std::string t = gn.title;
        if (t.size() > 34 && i != m_hover) t = t.substr(0, 32) + "…";
        auto lay = create_pango_layout(t);
        auto f = font;
        if (gn.project || i == m_hover) f.set_weight(Pango::Weight::BOLD);
        lay->set_font_description(f);
        int lw, lh;
        lay->get_pixel_size(lw, lh);
        cr->save();
        const double ring = (gn.project ? 3.0 : 0.0) + ((gn.id == m_current || i == m_center) ? 4.0 : 0.0);
        cr->translate(p.x, p.y + core::graph_radius(gn) + ring + 3);
        cr->scale(1 / m_zoom, 1 / m_zoom);
        source(cr, fg, 0.85 * al);
        cr->move_to(-lw / 2.0, 0);
        lay->show_in_cairo_context(cr);
        cr->restore();
    }
    cr->restore();
    if (map_shown()) draw_map(cr);   // s071g
}

// ── s071g: zoom, and the thumbnail ──────────────────────────────────────────
void GraphCanvas::zoom_at(double px, double py, double factor) {
    const double z = std::clamp(m_zoom * factor, 0.15, 4.0);
    const double k = z / m_zoom;
    m_ox = px - (px - m_ox) * k;
    m_oy = py - (py - m_oy) * k;
    m_zoom = z;
    queue_draw();
}

GraphCanvas::Box GraphCanvas::world_box() const {
    Box b{1e18, 1e18, -1e18, -1e18};
    for (std::size_t i = 0; i < m_pos.size() && i < m_g.nodes.size(); ++i) {
        const double r = core::graph_radius(m_g.nodes[i]) + 24;
        b.x0 = std::min(b.x0, m_pos[i].x - r); b.x1 = std::max(b.x1, m_pos[i].x + r);
        b.y0 = std::min(b.y0, m_pos[i].y - r); b.y1 = std::max(b.y1, m_pos[i].y + r);
    }
    return b;
}

bool GraphCanvas::map_shown() const {
    if (m_g.nodes.empty() || get_width() < 300 || get_height() < 200) return false;
    const Box b = world_box();
    double x0, y0, x1, y1;
    to_world(0, 0, x0, y0);
    to_world(get_width(), get_height(), x1, y1);
    return b.x0 < x0 || b.y0 < y0 || b.x1 > x1 || b.y1 > y1;   // something is off screen
}

void GraphCanvas::map_rect(double& x, double& y, double& w, double& h) const {
    w = 190;
    h = 120;
    x = get_width() - w - 14;
    y = get_height() - h - 14;
}

bool GraphCanvas::in_map(double px, double py) const {
    double x, y, w, h;
    map_rect(x, y, w, h);
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

// The map shows the bubbles AND the view, so either can be found from it.
namespace {
struct MapFrame { double s, mx, my, wx0, wy0; };
}

static MapFrame map_frame(const GraphCanvas::Box& world, const GraphCanvas::Box& view,
                          double x, double y, double w, double h) {
    const double x0 = std::min(world.x0, view.x0), y0 = std::min(world.y0, view.y0);
    const double x1 = std::max(world.x1, view.x1), y1 = std::max(world.y1, view.y1);
    const double s = std::min((w - 12) / std::max(1.0, x1 - x0), (h - 12) / std::max(1.0, y1 - y0));
    const double mx = x + (w - (x1 - x0) * s) / 2, my = y + (h - (y1 - y0) * s) / 2;
    return {s, mx, my, x0, y0};
}

void GraphCanvas::map_to_view(double px, double py) {
    double x, y, w, h;
    map_rect(x, y, w, h);
    Box view;
    to_world(0, 0, view.x0, view.y0);
    to_world(get_width(), get_height(), view.x1, view.y1);
    const MapFrame f = map_frame(world_box(), view, x, y, w, h);
    const double wx = f.wx0 + (std::clamp(px, x, x + w) - f.mx) / f.s;
    const double wy = f.wy0 + (std::clamp(py, y, y + h) - f.my) / f.s;
    m_ox = get_width() / 2.0 - wx * m_zoom;
    m_oy = get_height() / 2.0 - wy * m_zoom;
    queue_draw();
}

void GraphCanvas::draw_map(const Cairo::RefPtr<Cairo::Context>& cr) {
    GdkRGBA fgc;
    gtk_widget_get_color(GTK_WIDGET(gobj()), &fgc);
    const Gdk::RGBA fg(fgc.red, fgc.green, fgc.blue, 1.0);
    const Gdk::RGBA accent = rgba(appearance::accent_in_force());
    const bool dark = appearance::is_dark();
    const auto sc = appearance::state_colours();
    double x, y, w, h;
    map_rect(x, y, w, h);
    auto rounded = [&](double rx, double ry, double rw, double rh, double r) {
        cr->begin_new_path();
        cr->arc(rx + rw - r, ry + r, r, -M_PI / 2, 0);
        cr->arc(rx + rw - r, ry + rh - r, r, 0, M_PI / 2);
        cr->arc(rx + r, ry + rh - r, r, M_PI / 2, M_PI);
        cr->arc(rx + r, ry + r, r, M_PI, 3 * M_PI / 2);
        cr->close_path();
    };
    for (int i = 4; i >= 1; --i) {
        cr->set_source_rgba(0, 0, 0, dark ? 0.12 : 0.04);
        rounded(x - i * 0.5, y + i, w + i, h + i * 0.5, 9);
        cr->fill();
    }
    if (dark) cr->set_source_rgba(0.16, 0.16, 0.17, 0.96);
    else      cr->set_source_rgba(0.99, 0.99, 0.99, 0.96);
    rounded(x, y, w, h, 8);
    cr->fill_preserve();
    source(cr, fg, 0.15);
    cr->set_line_width(1);
    cr->stroke();

    Box view;
    to_world(0, 0, view.x0, view.y0);
    to_world(get_width(), get_height(), view.x1, view.y1);
    const MapFrame f = map_frame(world_box(), view, x, y, w, h);
    auto mx = [&](double wx) { return f.mx + (wx - f.wx0) * f.s; };
    auto my = [&](double wy) { return f.my + (wy - f.wy0) * f.s; };
    cr->save();
    rounded(x, y, w, h, 8);
    cr->clip();
    for (const auto& name : m_g.groups) {   // the halos, faint
        double cx, cy, r;
        if (!halo_of(m_g, m_pos, {}, name, cx, cy, r)) continue;
        source(cr, accent, 0.12);
        cr->begin_new_path();
        cr->arc(mx(cx), my(cy), r * f.s, 0, 2 * M_PI);
        cr->fill();
    }
    source(cr, fg, 0.18);
    cr->set_line_width(0.6);
    for (const auto& e : m_g.edges) {
        cr->move_to(mx(m_pos[static_cast<std::size_t>(e.a)].x), my(m_pos[static_cast<std::size_t>(e.a)].y));
        cr->line_to(mx(m_pos[static_cast<std::size_t>(e.b)].x), my(m_pos[static_cast<std::size_t>(e.b)].y));
    }
    cr->stroke();
    for (std::size_t i = 0; i < m_g.nodes.size(); ++i) {
        const auto& gn = m_g.nodes[i];
        Gdk::RGBA c = fg;
        double a = gn.project ? 0.7 : 0.4;
        if (gn.todo) {
            a = 1.0;
            switch (gn.state) {
                case core::RowState::Overdue:   c = rgba(sc.late); break;
                case core::RowState::DueToday:  c = rgba(sc.today); break;
                case core::RowState::Flagged:   c = rgba(sc.flagged); break;
                case core::RowState::Available: c = rgba(sc.available); break;
                case core::RowState::Done:      c = rgba(sc.done); break;
                default:                        c = fg; a = 0.4;
            }
        }
        if (!m_matches.empty() && !m_matches.count(static_cast<int>(i))) a *= 0.3;
        source(cr, c, a);
        cr->begin_new_path();
        cr->arc(mx(m_pos[i].x), my(m_pos[i].y), std::max(1.6, core::graph_radius(gn) * f.s), 0, 2 * M_PI);
        cr->fill();
    }
    // What is on screen.
    source(cr, accent, 0.14);
    cr->rectangle(mx(view.x0), my(view.y0), (view.x1 - view.x0) * f.s, (view.y1 - view.y0) * f.s);
    cr->fill_preserve();
    source(cr, accent, 0.9);
    cr->set_line_width(1.2);
    cr->stroke();
    cr->restore();
}

// ═════════════════════════════════════════════════════════════════════════════
// GraphPane
// ═════════════════════════════════════════════════════════════════════════════
GraphPane::GraphPane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL, 0),
      m_head(widgets::unregistered, "shell.graph.head", Gtk::Orientation::HORIZONTAL, 12),
      m_title(widgets::unregistered, "shell.graph.title", "Graph"),
      m_summary(widgets::unregistered, "shell.graph.summary"),
      m_everything(widgets::unregistered, "shell.graph.everything", "Show Everything"),
      m_group_btn(widgets::unregistered, "shell.graph.group_btn"),
      m_show_btn(widgets::unregistered, "shell.graph.show_btn"),
      m_find(widgets::unregistered, "shell.graph.find"),
      m_find_count(widgets::unregistered, "shell.graph.find_count"),
      m_g_none(widgets::unregistered, "shell.graph.group.none", "Nothing"),
      m_g_tag(widgets::unregistered, "shell.graph.group.tag", "Tag"),
      m_g_place(widgets::unregistered, "shell.graph.group.place", "Place"),
      m_g_project(widgets::unregistered, "shell.graph.group.project", "Project"),
      m_s_links(widgets::unregistered, "shell.graph.show.links", "Links"),
      m_s_tree(widgets::unregistered, "shell.graph.show.tree", "The tree"),
      m_s_tags(widgets::unregistered, "shell.graph.show.tags", "Shared tags"),
      m_s_done(widgets::unregistered, "shell.graph.show.done", "Done todos"),
      m_s_notes(widgets::unregistered, "shell.graph.show.notes", "Plain notes") {

    auto* words = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shell.graph.head.words",
                                                  Gtk::Orientation::VERTICAL, 0);
    m_title.add_css_class("jot-view-title");
    m_title.set_xalign(0);
    m_summary.add_css_class("dim-label");
    m_summary.set_xalign(0);
    m_summary.set_ellipsize(Pango::EllipsizeMode::END);
    words->append(m_title);
    words->append(m_summary);
    words->set_hexpand(true);
    m_head.append(*words);

    m_everything.add_css_class("pill");
    m_everything.set_valign(Gtk::Align::CENTER);
    m_everything.set_tooltip_text("Back to the whole graph (Esc)");
    m_everything.set_visible(false);
    m_everything.signal_clicked().connect([this]() { m_canvas.center_on(-1); });
    m_head.append(m_everything);

    // Group ▾
    {
        auto* col = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shell.graph.group.col",
                                                    Gtk::Orientation::VERTICAL, 2);
        col->set_margin(6);
        auto* head = Gtk::make_managed<widgets::Label>(widgets::unregistered, "shell.graph.group.head");
        head->set_markup("<b>Group</b>");
        head->set_xalign(0);
        col->append(*head);
        for (auto* b : {&m_g_none, &m_g_tag, &m_g_place, &m_g_project}) col->append(*b);
        m_g_tag.set_group(m_g_none);
        m_g_place.set_group(m_g_none);
        m_g_project.set_group(m_g_none);
        m_g_none.set_active(true);
        m_g_tag.set_tooltip_text("A clump for each tag (its first that is not a place)");
        m_g_place.set_tooltip_text("A clump for each place (#at/town ...)");
        m_g_project.set_tooltip_text("A clump for each project, with everything in it");
        auto* pop = Gtk::make_managed<widgets::Popover>(widgets::unregistered, "shell.graph.group.popover");
        pop->set_child(*col);
        m_group_btn.set_popover(*pop);
        m_group_btn.set_label("Group");
        m_group_btn.set_always_show_arrow(true);
        m_group_btn.set_tooltip_text("Gather the bubbles into clumps: by tag, place or project");
        m_group_btn.set_valign(Gtk::Align::CENTER);
        m_head.append(m_group_btn);
        for (auto* b : {&m_g_none, &m_g_tag, &m_g_place, &m_g_project})
            b->signal_toggled().connect([this, b]() {
                if (!b->get_active()) return;
                m_group = b == &m_g_tag ? core::GraphGroup::Tag
                        : b == &m_g_place ? core::GraphGroup::Place
                        : b == &m_g_project ? core::GraphGroup::Project : core::GraphGroup::None;
                m_group_btn.set_label(m_group == core::GraphGroup::None ? "Group" : b->get_label());
                if (m_applying) return;
                rebuild(true);
                m_canvas.fit();
                emit_view();
            });
    }
    // Show (the funnel)
    {
        auto* col = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shell.graph.show.col",
                                                    Gtk::Orientation::VERTICAL, 2);
        col->set_margin(6);
        auto* head = Gtk::make_managed<widgets::Label>(widgets::unregistered, "shell.graph.show.head");
        head->set_markup("<b>Show</b>");
        head->set_xalign(0);
        col->append(*head);
        for (auto* b : {&m_s_links, &m_s_tree, &m_s_tags, &m_s_done, &m_s_notes}) col->append(*b);
        m_s_links.set_tooltip_text("Lines for [links](jot:...) between notes");
        m_s_tree.set_tooltip_text("Lines from a note to the notes under it");
        m_s_tags.set_tooltip_text("Dashed lines between notes that share a tag (busy -- try Group instead)");
        auto* pop = Gtk::make_managed<widgets::Popover>(widgets::unregistered, "shell.graph.show.popover");
        pop->set_child(*col);
        m_show_btn.set_popover(*pop);
        m_show_btn.set_icon_name("jot-filter-symbolic");
        m_show_btn.set_tooltip_text("Show: which lines, which bubbles");
        m_show_btn.set_valign(Gtk::Align::CENTER);
        m_head.append(m_show_btn);
        for (auto* b : {&m_s_links, &m_s_tree, &m_s_tags, &m_s_done, &m_s_notes})
            b->signal_toggled().connect([this]() {
                if (m_applying) return;
                m_show.links = m_s_links.get_active();
                m_show.tree = m_s_tree.get_active();
                m_show.tags = m_s_tags.get_active();
                m_show.done = m_s_done.get_active();
                m_show.notes = m_s_notes.get_active();
                rebuild(true);
                m_canvas.fit();
                emit_view();
            });
    }
    m_find_count.add_css_class("dim-label");
    m_find_count.set_valign(Gtk::Align::CENTER);
    m_head.append(m_find_count);
    m_find.set_placeholder_text("Find on the graph");
    m_find.set_tooltip_text("Words, #tag, is:, due: -- like Find. The bubbles that match light up");
    m_find.set_size_request(220, -1);
    m_find.set_valign(Gtk::Align::CENTER);
    m_find.signal_search_changed().connect([this]() { run_find(); });
    m_find.signal_stop_search().connect([this]() {
        if (!m_find.get_text().empty()) m_find.set_text("");
        else m_canvas.signal_close().emit();
    });
    m_head.append(m_find);
    {
        auto* q = Gtk::make_managed<widgets::Button>(widgets::unregistered, "shell.graph.help", "?");
        q->add_css_class("jot-help-q");
        q->set_valign(Gtk::Align::CENTER);
        q->set_action_name("win.help-on");
        q->set_action_target_value(Glib::Variant<Glib::ustring>::create("graph"));
        q->set_tooltip_text("Help for the graph -- its page in the jot guide");
        m_head.append(*q);
    }
    m_head.set_margin_top(12);
    m_head.set_margin_bottom(10);
    m_head.set_margin_start(18);
    m_head.set_margin_end(14);
    append(m_head);
    append(m_canvas);

    m_canvas.signal_centered().connect([this](int i) {
        m_everything.set_visible(i >= 0);
        say_summary();
    });
    appearance::on_change([this]() { m_canvas.queue_draw(); });
}

void GraphPane::set_source(core::NodeSource* src) {
    m_src = src;
    m_canvas.center_on(-1);
    rebuild();
    m_canvas.fit();
}

void GraphPane::rebuild(bool settle) {
    if (!m_src) {
        m_canvas.set_graph({});
        say_summary();
        return;
    }
    m_canvas.set_graph(core::graph_build(*m_src, m_show, m_group, now_s()), settle);
    m_everything.set_visible(m_canvas.centered() >= 0);
    run_find();
    say_summary();
}

void GraphPane::refresh() { rebuild(); }

void GraphPane::opened() {
    rebuild();
    m_canvas.fit();
    m_find.grab_focus();
    const auto& g = m_canvas.graph();
    log_info("shown -- " + std::to_string(g.nodes.size()) + " bubble(s), " + std::to_string(g.edges.size()) +
             " line(s), group " + core::graph_group_word(m_group) + ", show " + core::graph_show_words(m_show));
}

void GraphPane::focus_find() { m_find.grab_focus(); }

void GraphPane::set_current(const core::NodeId& id) { m_canvas.set_current(id); }

void GraphPane::run_find() {
    const std::string q = m_find.get_text().raw();
    std::set<int> hits;
    if (m_src && core::query_active(q)) {
        const auto query = core::parse_query(q);
        const auto& g = m_canvas.graph();
        const auto now = now_s();
        for (std::size_t i = 0; i < g.nodes.size(); ++i)
            if (const core::Node* n = m_src->find(g.nodes[i].id); n && core::query_matches(*m_src, *n, query, now))
                hits.insert(static_cast<int>(i));
        m_find_count.set_text(hits.empty() ? "no match" : std::to_string(hits.size()) + (hits.size() == 1 ? " match" : " matches"));
        log_info("find '" + q + "' -> " + std::to_string(hits.size()));
    } else {
        m_find_count.set_text("");
    }
    m_canvas.set_matches(hits);
}

void GraphPane::say_summary() {
    const auto& g = m_canvas.graph();
    std::string s = std::to_string(g.nodes.size()) + (g.nodes.size() == 1 ? " note" : " notes") + " · " +
                    std::to_string(g.edges.size()) + (g.edges.size() == 1 ? " line" : " lines");
    if (!g.groups.empty()) s += " · " + std::to_string(g.groups.size()) + (g.groups.size() == 1 ? " group" : " groups");
    if (const int c = m_canvas.centered(); c >= 0)
        s = "Around “" + g.nodes[static_cast<std::size_t>(c)].title + "” · " +
            std::to_string(core::graph_around(g, c, 2).size()) + " of " + std::to_string(g.nodes.size());
    m_summary.set_text(s);
}

void GraphPane::set_view(const std::string& group, const std::string& show) {
    m_applying = true;
    m_group = core::graph_group_parse(group);
    m_show = core::graph_show_parse(show);
    switch (m_group) {
        case core::GraphGroup::None:    m_g_none.set_active(true); break;
        case core::GraphGroup::Tag:     m_g_tag.set_active(true); break;
        case core::GraphGroup::Place:   m_g_place.set_active(true); break;
        case core::GraphGroup::Project: m_g_project.set_active(true); break;
    }
    m_s_links.set_active(m_show.links);
    m_s_tree.set_active(m_show.tree);
    m_s_tags.set_active(m_show.tags);
    m_s_done.set_active(m_show.done);
    m_s_notes.set_active(m_show.notes);
    m_applying = false;
}

void GraphPane::emit_view() {
    const std::string show = core::graph_show_words(m_show);
    m_sig_view.emit(core::graph_group_word(m_group),
                    show == core::graph_show_words(core::GraphShow{}) ? std::string{} : show);
    log_info(std::string("group ") + core::graph_group_word(m_group) + ", show " + show);
}

}  // namespace jot
