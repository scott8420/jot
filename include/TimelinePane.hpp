#pragma once
#include "core/Nodes.hpp"
#include "core/Timeline.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/drawingarea.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/gesturedrag.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/eventcontrollerkey.h>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// TimelinePane -- J5 (s059). Scott: "a horizontal running calendar with clumps
// of dots with a line attaching to the date. In the clump is projects, tasks,
// notes. If things get cluttered we stagger ... Right-click expands the
// project's tasks." Then: the calendar ON TOP, the work hanging DOWN in a
// window that scrolls; a find that takes the keys; an inset thumbnail to move
// around; a Someday filter.
//
// It takes the NOTE's place in the window (the centre control's fourth
// button, Ctrl+Shift+L); Note details stays beside it and follows a click.
//
//   head      "Timeline", what it adds up to, and the find field
//   controls  Week | Month | Season, the chips (Projects, Todos, Notes,
//             Someday), Today
//   canvas    TimelineCanvas: the strip pinned at the top, the feeders'
//             threads just under it, the clumps below in a body that scrolls
//             both ways, the thumbnail in the corner
//
// What lands where is core/Timeline (selftested); this file draws it and
// handles the hand. One click picks (Note details shows it), a double-click
// opens the note, a right-click opens or closes a clump.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class TimelineCanvas : public Gtk::DrawingArea {
public:
    enum class Zoom { Week, Month, Season };

    TimelineCanvas();
    ~TimelineCanvas() override;

    void set_model(const core::NodeSource* src, core::Timeline t, std::int64_t now);
    void set_zoom(Zoom z);
    void set_group(core::TlGroup g);           // s060: Day | Place | Purpose
    core::TlGroup group() const { return m_group; }
    Zoom zoom() const { return m_zoom; }
    void set_matches(std::vector<core::NodeId> ids);   // find: these stay bright
    int  step_match(int dir);                          // Enter / Shift+Enter: the index now current, -1 none
    void reveal(const core::NodeId& id);               // scroll it into view, open its clump if need be
    void set_current(const core::NodeId& id);          // the note on show: a ring
    void go_today();
    std::size_t match_count() const { return m_matches.size(); }
    int match_cur() const { return m_match_cur; }
    const core::Timeline& timeline() const { return m_tl; }
    const core::NodeId& match_id(int i) const { return m_matches[static_cast<std::size_t>(i)]; }

    sigc::signal<void(core::NodeId)>& signal_pick() { return m_sig_pick; }
    sigc::signal<void(core::NodeId)>& signal_open() { return m_sig_open; }
    sigc::signal<void()>&              signal_close() { return m_sig_close; }
    sigc::signal<void(Zoom)>&          signal_zoomed() { return m_sig_zoomed; }
    // s063: a line dragged to another day -- (id, why it was where it was,
    // riding in a project's card, the day it was dropped on).
    sigc::signal<void(core::NodeId, core::TlWhy, bool, std::int64_t)>& signal_move() { return m_sig_move; }

    // For the log and the selftest-by-eye: what is laid out.
    std::size_t clump_count() const { return m_clumps.size(); }

private:
    struct Row {
        core::NodeId id;
        core::TlKind kind  = core::TlKind::Todo;
        core::TlWhy  why   = core::TlWhy::Due;
        core::TlTone tone  = core::TlTone::Available;
        int          estimate = 0;
        std::string  title;
        int          steps = 0;        // steps it carries (shown as a count when closed)
        bool         step  = false;    // a step, under its carrier (open clump)
        int          more  = 0;        // a "+N more" row
        std::vector<core::NodeId> carries;   // ids this row stands for in find (itself + its steps)
    };
    struct Clump {
        std::string  key;              // "d<day>" or "s<parent>"
        std::int64_t day = 0;          // 0 for Someday
        std::string  head;             // "Tue 13 Oct"
        double       anchor = 0;       // the x its leader drops from (content coords)
        double       x = 0, y = 0, w = 0, h = 0;
        bool         compact = false;  // Season: dots, no words
        int          lane = 0;         // s060: which lane it hangs in
        bool         open = false;
        std::vector<Row> rows;         // full: one per line; compact: one per dot
    };

    // s060: a lane -- the whole span's width, one place or purpose.
    struct Lane {
        std::string key, title, line;
        double y = 0, h = 0;           // content
        double body_top = 0;           // where its cards start
    };

    // Layout (content coordinates: x from the first day, y from the body's top).
    void layout();
    double px_per_day() const;
    double clump_width() const;
    double day_x(std::int64_t day) const;         // a day's centre
    double strip_h() const;                       // the strip and the threads under it
    double view_w() const { return get_width(); }
    double view_h() const { return std::max(0.0, get_height() - strip_h()); }
    void   clamp_offsets();
    void   keep_centre(const std::function<void()>& change);

    // Drawing.
    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void draw_body(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void draw_clump(const Cairo::RefPtr<Cairo::Context>& cr, const Clump& c, double ox, double oy);
    void draw_strip(const Cairo::RefPtr<Cairo::Context>& cr, int w);
    void draw_threads(const Cairo::RefPtr<Cairo::Context>& cr, int w);
    void draw_thumb(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    core::TlRect thumb_rect(int w, int h) const;
    bool thumb_shown() const;

    // Hits (widget coordinates).
    const Clump* clump_at(double x, double y) const;
    const Row*   row_at(double x, double y, const Clump** in = nullptr) const;

    // Events.
    void on_press(int n, double x, double y);
    void on_right(int n, double x, double y);
    void on_drag_begin(double x, double y);
    void on_drag_update(double dx, double dy);
    void on_drag_end(double dx, double dy);
    bool on_scroll(double dx, double dy);
    void on_motion(double x, double y);
    bool on_key(guint key, guint code, Gdk::ModifierType mods);
    bool on_tooltip(int x, int y, bool kbd, const Glib::RefPtr<Gtk::Tooltip>& tip);

    const core::NodeSource* m_src = nullptr;
    core::Timeline m_tl;
    std::int64_t   m_now = 0;
    std::int64_t   m_today = 0;
    Zoom           m_zoom = Zoom::Month;
    std::vector<Clump> m_clumps;
    std::vector<Lane>  m_lanes;                // s060
    core::TlGroup      m_group = core::TlGroup::Day;
    std::set<std::string> m_open;             // clumps opened by a right-click (or a find)
    std::string m_pin_key;                    // the clump last opened or closed stays where it was ...
    double      m_pin_y = -1;                 // ... at this top (content); cleared by a zoom
    double m_cw = 0, m_ch = 0;                // content size
    double m_ox = 0, m_oy = 0;                // the view's offset into it
    double m_someday_x = 0;                   // where the Someday region starts (0 = none)
    bool   m_homed = false;                   // the first size puts today in view

    std::vector<core::NodeId> m_matches;
    std::unordered_set<core::NodeId> m_match_set;
    std::set<std::int64_t> m_match_days;
    int  m_match_cur = -1;
    core::NodeId m_current;
    const Row* m_hover = nullptr;

    Gtk::EventControllerScroll* m_scroll = nullptr;   // owned by the widget; asked for modifiers and units
    enum class Drag { None, Pan, Nav, Move, Cancelled };   // s063: Move -- a line to another day
    Drag   m_drag = Drag::None;
    double m_drag_x = 0, m_drag_y = 0, m_drag_ox = 0, m_drag_oy = 0;
    // s063: the line in hand.
    core::NodeId m_move_id;
    core::TlWhy  m_move_why = core::TlWhy::Due;
    bool         m_move_step = false;
    std::string  m_move_title;
    double       m_px = 0, m_py = 0;       // the pointer, widget coordinates
    std::int64_t m_drop_day = 0;           // the day under it (0 = none)
    std::int64_t day_at_x(double x) const; // widget x -> a day of the span, or 0
    void draw_move(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void draw_links(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);   // s064
    void end_move(bool drop);

    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    sigc::signal<void(core::NodeId)> m_sig_pick, m_sig_open;
    sigc::signal<void()>              m_sig_close;
    sigc::signal<void(Zoom)>          m_sig_zoomed;
    sigc::signal<void(core::NodeId, core::TlWhy, bool, std::int64_t)> m_sig_move;   // s063
};

class TimelinePane : public widgets::Box {
public:
    explicit TimelinePane(std::string_view name);

    void set_source(core::NodeSource* src);
    void refresh();                          // re-read the model; keep the place
    void opened();                           // shown: re-read, and the find field takes the keys
    void focus_find();                       // Ctrl+F while it is on show
    void set_current(const core::NodeId& id);

    sigc::signal<void(core::NodeId)>& signal_pick() { return m_canvas.signal_pick(); }
    sigc::signal<void(core::NodeId)>& signal_open() { return m_canvas.signal_open(); }
    sigc::signal<void()>&              signal_close() { return m_canvas.signal_close(); }

private:
    void run_find(bool scroll);
    void step(int dir);
    void say_count();
    void sync_zoom_buttons();
    void on_move(const core::NodeId& id, core::TlWhy why, bool riding, std::int64_t day);   // s063

    core::NodeSource* m_src = nullptr;
    core::TlShow      m_show;

    widgets::Box          m_head;
    widgets::Label        m_title;
    widgets::Label        m_summary;
    widgets::SearchEntry  m_find;
    widgets::Label        m_find_count;
    widgets::Box          m_controls;
    widgets::ToggleButton m_week, m_month, m_season;
    widgets::ToggleButton m_g_day, m_g_place, m_g_purpose;   // s060
    widgets::ToggleButton m_chip_projects, m_chip_todos, m_chip_notes, m_chip_someday;
    widgets::ToggleButton m_chip_links;   // s064
    widgets::Button       m_today;
    TimelineCanvas        m_canvas;
    bool                  m_syncing = false;
};

}  // namespace jot
