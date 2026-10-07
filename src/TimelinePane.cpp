#include "TimelinePane.hpp"
#include "Appearance.hpp"
#include "Log.hpp"
#include "core/Tasks.hpp"
#include "core/Undo.hpp"

#include <gdkmm/rgba.h>
#include <glibmm/markup.h>
#include <gtkmm/tooltip.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <pangomm/layout.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>

namespace jot {

namespace {

// ── metrics ─────────────────────────────────────────────────────────────────
constexpr double kMonthRow = 20;    // the strip: month names ...
constexpr double kDayRow   = 34;    // ... over the days
constexpr double kThreadGap = 8;
constexpr double kThreadRow = 11;
constexpr std::size_t kMaxThreads = 5;
constexpr double kLeft     = 28;    // content x of the first day's left edge
constexpr double kBodyTop  = 18;    // the first clump's distance under the strip
constexpr double kHead     = 24;    // a clump's day line
constexpr double kRow      = 21;
constexpr double kPadB     = 8;
constexpr double kLaneHead = 40;   // s060: a lane's name and its line
constexpr std::size_t kClosedRows = 5;   // a closed clump shows this many, then "+N more"
constexpr double kDot      = 13;    // compact (Season) dot pitch
constexpr std::size_t kCompactDots = 12;
constexpr double kThumbW   = 210, kThumbH = 66, kThumbM = 14;

std::int64_t now_s() { return static_cast<std::int64_t>(std::time(nullptr)); }

std::tm local(std::int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    return tm;
}

std::string fmt(std::int64_t t, const char* f) {
    const std::tm tm = local(t);
    char buf[64];
    std::strftime(buf, sizeof buf, f, &tm);
    std::string s = buf;
    if (auto p = s.find("  "); p != std::string::npos) s.erase(p, 1);
    return s;
}

std::string day_head(std::int64_t day, std::int64_t today) {
    if (day == today) return "Today";
    if (day == core::day_at(today, 1)) return "Tomorrow";
    if (day == core::day_at(today, -1)) return "Yesterday";
    return local(day).tm_year == local(today).tm_year ? fmt(day, "%a %e %b") : fmt(day, "%a %e %b %Y");
}

void rounded(const Cairo::RefPtr<Cairo::Context>& cr, double x, double y, double w, double h, double r) {
    r = std::min({r, w / 2, h / 2});
    cr->begin_new_sub_path();
    cr->arc(x + w - r, y + r, r, -M_PI / 2, 0);
    cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
    cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
    cr->arc(x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cr->close_path();
}

Gdk::RGBA rgba(const std::string& css) {
    Gdk::RGBA c;
    if (!c.set(css)) c.set_rgba(0.21, 0.52, 0.89, 1.0);
    return c;
}

void source(const Cairo::RefPtr<Cairo::Context>& cr, const Gdk::RGBA& c, double alpha = 1.0) {
    cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), c.get_alpha() * alpha);
}

// A font a fraction of the widget's own size. The theme's size may be
// ABSOLUTE (device units) or in points; scaling it the wrong way makes a
// "smaller" line come out bigger than the rest.
Pango::FontDescription scaled(Pango::FontDescription f, double frac,
                              Pango::Weight weight = Pango::Weight::NORMAL) {
    const int size = f.get_size() ? f.get_size() : 10 * PANGO_SCALE;
    const int want = static_cast<int>(size * frac);
    if (f.get_size_is_absolute()) f.set_absolute_size(want);
    else f.set_size(want);
    if (weight != Pango::Weight::NORMAL) f.set_weight(weight);
    return f;
}

double dot_r(int est) {
    if (est <= 0) return 4.5;
    return std::clamp(3.5 + std::sqrt(static_cast<double>(est)) / 2.4, 4.0, 7.5);
}

std::string why_words(core::TlWhy w, std::int64_t day, std::int64_t today) {
    const std::string d = day_head(day, today);
    switch (w) {
    case core::TlWhy::Due:     return "due " + d;
    case core::TlWhy::Starts:  return "starts " + d;
    case core::TlWhy::Done:    return "done " + d;
    case core::TlWhy::Made:    return "made " + d;
    case core::TlWhy::Someday: return "someday";
    }
    return {};
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// TimelineCanvas
// ═════════════════════════════════════════════════════════════════════════════
TimelineCanvas::TimelineCanvas() {
    set_name("shell.timeline.canvas");
    set_hexpand(true);
    set_vexpand(true);
    set_focusable(true);
    set_has_tooltip(true);
    set_draw_func(sigc::mem_fun(*this, &TimelineCanvas::on_draw));

    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_released().connect(sigc::mem_fun(*this, &TimelineCanvas::on_press));
    add_controller(click);

    auto right = Gtk::GestureClick::create();
    right->set_button(GDK_BUTTON_SECONDARY);
    right->signal_pressed().connect(sigc::mem_fun(*this, &TimelineCanvas::on_right));
    add_controller(right);

    auto drag = Gtk::GestureDrag::create();
    drag->set_button(GDK_BUTTON_PRIMARY);
    drag->signal_drag_begin().connect(sigc::mem_fun(*this, &TimelineCanvas::on_drag_begin));
    drag->signal_drag_update().connect(sigc::mem_fun(*this, &TimelineCanvas::on_drag_update));
    drag->signal_drag_end().connect(sigc::mem_fun(*this, &TimelineCanvas::on_drag_end));
    add_controller(drag);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
    scroll->signal_scroll().connect(sigc::mem_fun(*this, &TimelineCanvas::on_scroll), false);
    m_scroll = scroll.get();
    add_controller(scroll);

    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect(sigc::mem_fun(*this, &TimelineCanvas::on_motion));
    motion->signal_leave().connect([this]() {
        if (m_hover) { m_hover = nullptr; queue_draw(); }
    });
    add_controller(motion);

    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(sigc::mem_fun(*this, &TimelineCanvas::on_key), false);
    add_controller(keys);

    signal_query_tooltip().connect(sigc::mem_fun(*this, &TimelineCanvas::on_tooltip), false);
    signal_resize().connect([this](int w, int) {
        if (!m_homed && w > 0 && m_tl.first) { go_today(); m_homed = true; }
        clamp_offsets();
    });

    std::weak_ptr<bool> alive = m_alive;
    appearance::on_change([this, alive]() {
        if (auto a = alive.lock(); a && *a) queue_draw();
    });
}

TimelineCanvas::~TimelineCanvas() { *m_alive = false; }

double TimelineCanvas::px_per_day() const {
    switch (m_zoom) {
    case Zoom::Week:   return 128;
    case Zoom::Month:  return 36;
    case Zoom::Season: return 12;
    }
    return 36;
}

double TimelineCanvas::clump_width() const { return m_zoom == Zoom::Week ? 156 : 180; }

double TimelineCanvas::day_x(std::int64_t day) const {
    return kLeft + (core::day_index(m_tl.first, day) + 0.5) * px_per_day();
}

double TimelineCanvas::strip_h() const {
    const std::size_t n = std::min(m_tl.threads.size(), kMaxThreads);
    return kMonthRow + kDayRow + (n ? kThreadGap + n * kThreadRow : 0);
}

void TimelineCanvas::clamp_offsets() {
    m_ox = std::clamp(m_ox, 0.0, std::max(0.0, m_cw - view_w()));
    m_oy = std::clamp(m_oy, 0.0, std::max(0.0, m_ch - view_h()));
}

void TimelineCanvas::keep_centre(const std::function<void()>& change) {
    const double ppd = px_per_day();
    const double frac = (m_ox + view_w() / 2 - kLeft) / ppd;
    change();
    m_ox = kLeft + frac * px_per_day() - view_w() / 2;
    clamp_offsets();
    queue_draw();
}

void TimelineCanvas::set_model(const core::NodeSource* src, core::Timeline t, std::int64_t now) {
    const bool first = !m_tl.first;
    // The view keeps its DAY across a re-read: the span can grow at the left.
    const double day_left = m_tl.first ? (m_ox - kLeft) / px_per_day() : 0;
    const std::int64_t old_first = m_tl.first;
    m_src   = src;
    m_tl    = std::move(t);
    m_now   = now;
    m_today = core::day_start(now);
    layout();
    if (!first && old_first) {
        m_ox = kLeft + (day_left + core::day_index(m_tl.first, old_first)) * px_per_day();
        clamp_offsets();
    } else if (get_width() > 0) {
        go_today();
        m_homed = true;
    }
    queue_draw();
}

void TimelineCanvas::set_zoom(Zoom z) {
    if (z == m_zoom) return;
    m_pin_key.clear();
    keep_centre([&] { m_zoom = z; layout(); });
    m_sig_zoomed.emit(z);
}

void TimelineCanvas::set_group(core::TlGroup g) {
    if (g == m_group) return;
    m_group = g;
    m_pin_key.clear();
    m_oy = 0;
    layout();
    queue_draw();
    if (auto lg = log::get(log::Area::Shell)) {
        std::string names;
        for (const auto& l : m_lanes) names += (names.empty() ? "" : " | ") + l.title + ": " + l.line;
        lg->info("timeline: grouped by {} -- {} lane(s){}{}", core::tl_group_word(g), m_lanes.size(),
                 names.empty() ? "" : " -- ", names);
    }
}

void TimelineCanvas::layout() {
    m_hover = nullptr;
    m_clumps.clear();
    m_someday_x = 0;
    if (!m_tl.first) { m_cw = m_ch = 0; return; }
    const double ppd = px_per_day();
    const int ndays = core::day_index(m_tl.first, m_tl.last) + 1;
    const double days_w = kLeft + ndays * ppd + 28;

    auto step_row = [&](const core::NodeId& id) {
        Row r;
        r.id = id;
        r.step = true;
        r.carries = {id};
        if (const core::Node* n = m_src ? m_src->find(id) : nullptr) {
            r.title = n->title.empty() ? std::string("Untitled") : n->title;
            r.estimate = n->task.estimate;
        }
        r.tone = m_src ? core::tl_tone(*m_src, id, m_now) : core::TlTone::Available;
        return r;
    };
    auto build = [&](Clump& c, const std::vector<core::TlItem>& items) {
        c.open = m_open.count(c.key) != 0;
        c.compact = m_zoom == Zoom::Season && !c.open;
        const std::size_t cap = c.compact ? kCompactDots : (c.open ? items.size() : kClosedRows);
        std::size_t shown = 0;
        Row more;
        for (const auto& it : items) {
            if (shown >= cap) {
                ++more.more;
                more.carries.push_back(it.id);
                more.carries.insert(more.carries.end(), it.steps.begin(), it.steps.end());
                continue;
            }
            Row r;
            r.id = it.id;
            r.kind = it.kind;
            r.why = it.why;
            r.tone = it.tone;
            r.estimate = it.estimate;
            r.title = it.title;
            r.steps = static_cast<int>(it.steps.size());
            r.carries.push_back(it.id);
            r.carries.insert(r.carries.end(), it.steps.begin(), it.steps.end());
            c.rows.push_back(std::move(r));
            ++shown;
            if (c.open)
                for (const auto& s : it.steps) c.rows.push_back(step_row(s));
        }
        if (more.more) c.rows.push_back(std::move(more));
        if (c.compact) {
            c.w = 34;
            const std::size_t n = c.rows.size();
            c.h = 22 + std::ceil(n / 2.0) * kDot + 4;
        } else {
            c.w = clump_width();
            c.h = kHead + c.rows.size() * kRow + kPadB;
        }
    };

    // s060: the work in LANES -- one nameless lane for Day, else one per
    // place or purpose (core::timeline_lanes). Each lane stacks its own cards.
    const auto lanes = m_src ? core::timeline_lanes(*m_src, m_tl, m_group, m_now)
                             : std::vector<core::TlLane>{};
    // Someday: after the last day, one card per parent -- what it is filed
    // under is the only shape undated work has. One region for every lane.
    auto someday_groups = [&](const std::vector<core::TlItem>& items,
                              std::vector<std::string>& order,
                              std::map<std::string, std::vector<core::TlItem>>& groups) {
        for (const auto& it : items) {
            const core::Node* n = m_src ? m_src->find(it.id) : nullptr;
            const std::string parent = n ? n->parent_id : std::string();
            if (!groups.count(parent)) order.push_back(parent);
            groups[parent].push_back(it);
        }
    };
    int cols = 0;
    for (const auto& l : lanes) {
        std::vector<std::string> order;
        std::map<std::string, std::vector<core::TlItem>> groups;
        someday_groups(l.someday, order, groups);
        cols = std::max(cols, std::min(3, static_cast<int>(order.size())));
    }
    const double cw = clump_width();
    double width = days_w;
    if (cols > 0) {
        m_someday_x = days_w + 40;
        width = m_someday_x + cols * (cw + 16) + 28;
    }

    m_lanes.clear();
    double y = 0;
    for (std::size_t li = 0; li < lanes.size(); ++li) {
        const auto& lane = lanes[li];
        Lane L;
        L.key = lane.key;
        L.title = lane.title;
        L.line = lane.line;
        L.y = y;
        const bool named = !lane.title.empty();
        L.body_top = y + (named ? kLaneHead : 0) + (named ? 8 : kBodyTop);
        const std::size_t first = m_clumps.size();
        for (const auto& d : lane.days) {
            Clump c;
            c.key = lane.key + "|d" + std::to_string(d.day);
            c.lane = static_cast<int>(li);
            c.day = d.day;
            c.head = day_head(d.day, m_today);
            c.anchor = day_x(d.day);
            build(c, d.items);
            c.x = std::max(4.0, c.anchor - c.w / 2);
            m_clumps.push_back(std::move(c));
        }
        if (!lane.someday.empty()) {
            std::vector<std::string> order;
            std::map<std::string, std::vector<core::TlItem>> groups;
            someday_groups(lane.someday, order, groups);
            const int lc = std::max(1, std::min(3, static_cast<int>(order.size())));
            int k = 0;
            for (const auto& parent : order) {
                Clump c;
                c.key = lane.key + "|s" + parent;
                c.lane = static_cast<int>(li);
                const core::Node* p = m_src && !parent.empty() ? m_src->find(parent) : nullptr;
                c.head = p ? (p->title.empty() ? std::string("Untitled") : p->title) : std::string("Top level");
                build(c, groups[parent]);
                if (c.compact) {  // Someday keeps its words: there is no day to lean on
                    c.open = true;
                    c.rows.clear();
                    m_open.insert(c.key);
                    build(c, groups[parent]);
                }
                c.anchor = m_someday_x + (k % lc) * (cw + 16) + cw / 2;
                c.x = c.anchor - c.w / 2;
                ++k;
                m_clumps.push_back(std::move(c));
            }
        }
        std::vector<core::TlSpan> spans;
        std::vector<double> pins;
        for (std::size_t i = first; i < m_clumps.size(); ++i) {
            spans.push_back({m_clumps[i].x, m_clumps[i].x + m_clumps[i].w, m_clumps[i].h});
            pins.push_back(!m_pin_key.empty() && m_clumps[i].key == m_pin_key ? m_pin_y : -1);
        }
        const auto tops = core::stack_down(spans, 10, 12, pins);
        double bottom = L.body_top;
        for (std::size_t i = first; i < m_clumps.size(); ++i) {
            m_clumps[i].y = L.body_top + tops[i - first];
            bottom = std::max(bottom, m_clumps[i].y + m_clumps[i].h);
        }
        L.h = (bottom - y) + (named ? 22 : 0);
        y += L.h;
        m_lanes.push_back(std::move(L));
    }
    m_cw = width;
    m_ch = y + 90;

    m_match_days.clear();
    if (!m_match_set.empty())
        for (const auto& c : m_clumps)
            for (const auto& r : c.rows)
                for (const auto& id : r.carries)
                    if (m_match_set.count(id) && c.day) m_match_days.insert(c.day);
    clamp_offsets();
}

void TimelineCanvas::set_matches(std::vector<core::NodeId> ids) {
    m_matches = std::move(ids);
    m_match_set = {m_matches.begin(), m_matches.end()};
    m_match_cur = -1;
    layout();
    queue_draw();
}

int TimelineCanvas::step_match(int dir) {
    if (m_matches.empty()) return -1;
    const int n = static_cast<int>(m_matches.size());
    if (m_match_cur < 0) m_match_cur = dir > 0 ? 0 : n - 1;
    else m_match_cur = ((m_match_cur + dir) % n + n) % n;
    queue_draw();
    return m_match_cur;
}

void TimelineCanvas::set_current(const core::NodeId& id) {
    if (id == m_current) return;
    m_current = id;
    queue_draw();
}

void TimelineCanvas::go_today() {
    if (!m_tl.first) return;
    m_ox = day_x(m_today) - view_w() * 0.3;
    m_oy = 0;
    clamp_offsets();
    queue_draw();
}

void TimelineCanvas::reveal(const core::NodeId& id) {
    auto locate = [&](bool& visible) -> const Clump* {
        for (const auto& c : m_clumps)
            for (const auto& r : c.rows) {
                if (r.id == id && !r.more) { visible = true; return &c; }
                for (const auto& x : r.carries)
                    if (x == id) { visible = false; return &c; }
            }
        return nullptr;
    };
    bool visible = false;
    const Clump* c = locate(visible);
    if (!c) return;
    if (!visible) {
        // Under "+N more", or a step in a closed clump: open it.
        m_open.insert(c->key);
        layout();
        c = locate(visible);
        if (!c) return;
    }
    const double vw = view_w(), vh = view_h();
    if (c->x < m_ox + 16 || c->x + c->w > m_ox + vw - 16) m_ox = c->anchor - vw / 2;
    if (c->y < m_oy + 8 || c->y + std::min(c->h, vh * 0.6) > m_oy + vh) {
        m_oy = c->y - kBodyTop;
        // In a named lane, keep its name in view when the card is near it.
        if (c->lane >= 0 && c->lane < static_cast<int>(m_lanes.size())) {
            const Lane& L = m_lanes[static_cast<std::size_t>(c->lane)];
            if (!L.title.empty() && c->y - L.y < vh / 2) m_oy = L.y;
        }
    }
    clamp_offsets();
    queue_draw();
}

// ── hits ────────────────────────────────────────────────────────────────────
const TimelineCanvas::Clump* TimelineCanvas::clump_at(double x, double y) const {
    const double top = strip_h();
    if (y < top) return nullptr;
    const double cx = x + m_ox, cy = y - top + m_oy;
    for (auto it = m_clumps.rbegin(); it != m_clumps.rend(); ++it)
        if (cx >= it->x && cx < it->x + it->w && cy >= it->y && cy < it->y + it->h) return &*it;
    return nullptr;
}

const TimelineCanvas::Row* TimelineCanvas::row_at(double x, double y, const Clump** in) const {
    const Clump* c = clump_at(x, y);
    if (in) *in = c;
    if (!c) return nullptr;
    const double cx = x + m_ox - c->x, cy = y - strip_h() + m_oy - c->y;
    if (c->compact) {
        for (std::size_t i = 0; i < c->rows.size(); ++i) {
            const double dx = 10 + (i % 2) * 14, dy = 22 + (i / 2) * kDot;
            if (std::hypot(cx - dx, cy - dy) <= 7) return &c->rows[i];
        }
        return nullptr;
    }
    if (cy < kHead) return nullptr;
    const std::size_t i = static_cast<std::size_t>((cy - kHead) / kRow);
    return i < c->rows.size() ? &c->rows[i] : nullptr;
}

core::TlRect TimelineCanvas::thumb_rect(int w, int h) const {
    return {w - kThumbW - kThumbM, h - kThumbH - kThumbM, kThumbW, kThumbH};
}

bool TimelineCanvas::thumb_shown() const {
    return m_tl.first && (m_cw > view_w() + 1 || m_ch > view_h() + 1);
}

// ── events ──────────────────────────────────────────────────────────────────
void TimelineCanvas::on_press(int n, double x, double y) {
    grab_focus();
    if (m_drag == Drag::Nav) return;
    if (thumb_shown()) {
        const auto t = thumb_rect(get_width(), get_height());
        if (x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h) return;
    }
    const Clump* c = nullptr;
    const Row* r = row_at(x, y, &c);
    if (!r) return;
    if (r->more) {   // "+N more": open the clump
        m_open.insert(c->key);
        layout();
        queue_draw();
        return;
    }
    if (n >= 2) m_sig_open.emit(r->id);
    else m_sig_pick.emit(r->id);
}

void TimelineCanvas::on_right(int, double x, double y) {
    const Clump* c = clump_at(x, y);
    if (!c) return;
    const std::string key = c->key;
    const double was_y = c->y;
    if (m_open.count(key)) m_open.erase(key); else m_open.insert(key);
    if (auto lg = log::get(log::Area::Shell))
        lg->info("timeline: clump '{}' {} (was at y {:.0f})", c->head, m_open.count(key) ? "opened" : "closed", was_y);
    // It stays where it is, under the hand that opened it; the clumps it now
    // touches make room below.
    m_pin_key = key;
    m_pin_y = was_y - (c->lane >= 0 && c->lane < static_cast<int>(m_lanes.size())
                           ? m_lanes[static_cast<std::size_t>(c->lane)].body_top : kBodyTop);
    layout();
    if (auto lg = log::get(log::Area::Shell))
        lg->debug("timeline: view at {:.0f},{:.0f} of {:.0f}x{:.0f}", m_ox, m_oy, m_cw, m_ch);
    queue_draw();
}

void TimelineCanvas::on_drag_begin(double x, double y) {
    m_drag_x = x;
    m_drag_y = y;
    m_drag_ox = m_ox;
    m_drag_oy = m_oy;
    m_drag = Drag::None;
    m_move_id.clear();
    if (thumb_shown()) {
        const auto t = thumb_rect(get_width(), get_height());
        if (x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h) {
            m_drag = Drag::Nav;
            core::thumb_to_view(m_cw, m_ch, t.w, t.h, x - t.x, y - t.y, view_w(), view_h(), m_ox, m_oy);
            queue_draw();
            return;
        }
    }
    // s063: pressed on a line that can move -- a drag takes IT, not the canvas.
    // Notes (on the day made) and done work (on the day ticked) are history.
    if (const Row* r = row_at(x, y); r && !r->more && !r->id.empty() &&
        (r->step || r->why == core::TlWhy::Due || r->why == core::TlWhy::Starts ||
         r->why == core::TlWhy::Someday)) {
        grab_focus();   // so Esc mid-drag reaches the canvas, not the find field
        m_move_id = r->id;
        m_move_why = r->why;
        m_move_step = r->step;
        m_move_title = r->title;
    }
}

std::int64_t TimelineCanvas::day_at_x(double x) const {
    if (!m_tl.first) return 0;
    const double cx = x + m_ox;
    if (m_someday_x > 0 && cx >= m_someday_x) return 0;
    const int i = static_cast<int>(std::floor((cx - kLeft) / px_per_day()));
    const int ndays = core::day_index(m_tl.first, m_tl.last) + 1;
    if (i < 0 || i >= ndays) return 0;
    return core::day_at(m_tl.first, i);
}

void TimelineCanvas::end_move(bool drop) {
    const core::NodeId id = m_move_id;
    const core::TlWhy why = m_move_why;
    const bool step = m_move_step;
    const std::int64_t day = m_drop_day;
    m_drag = Drag::Cancelled;
    m_move_id.clear();
    m_drop_day = 0;
    set_cursor("");
    queue_draw();
    if (auto lg = log::get(log::Area::Shell))
        lg->info("timeline: drag {} -- {}", drop && day ? "dropped" : "let go", drop && day ? fmt(day, "%a %e %b") : "no day");
    if (drop && day && !id.empty()) m_sig_move.emit(id, why, step, day);
}

void TimelineCanvas::on_drag_update(double dx, double dy) {
    if (m_drag == Drag::Nav) {
        const auto t = thumb_rect(get_width(), get_height());
        core::thumb_to_view(m_cw, m_ch, t.w, t.h, m_drag_x + dx - t.x, m_drag_y + dy - t.y,
                            view_w(), view_h(), m_ox, m_oy);
        queue_draw();
        return;
    }
    if (m_drag == Drag::Cancelled) return;   // Esc mid-drag: the rest of it is ignored
    if (m_drag == Drag::None && std::abs(dx) + std::abs(dy) > 4) {
        m_drag = m_move_id.empty() ? Drag::Pan : Drag::Move;
        set_cursor("grabbing");
    }
    if (m_drag == Drag::Move) {
        m_px = m_drag_x + dx;
        m_py = m_drag_y + dy;
        // Near an edge, the timeline runs under the hand.
        const double w = get_width();
        if (m_px < 40) m_ox -= 16;
        else if (m_px > w - 40) m_ox += 16;
        clamp_offsets();
        m_drop_day = day_at_x(m_px);
        queue_draw();
        return;
    }
    if (m_drag == Drag::Pan) {
        m_ox = m_drag_ox - dx;
        m_oy = m_drag_oy - dy;
        clamp_offsets();
        queue_draw();
    }
}

void TimelineCanvas::on_drag_end(double, double) {
    if (m_drag == Drag::Move) { end_move(true); m_drag = Drag::None; return; }
    if (m_drag == Drag::Pan) set_cursor("");
    m_drag = Drag::None;
    m_move_id.clear();
}


bool TimelineCanvas::on_scroll(double dx, double dy) {
    const auto mods = m_scroll ? m_scroll->get_current_event_state() : Gdk::ModifierType{};
    // A wheel says "one notch"; a touchpad says pixels.
    const bool pixels = m_scroll &&
        gtk_event_controller_scroll_get_unit(m_scroll->gobj()) == GDK_SCROLL_UNIT_SURFACE;
    const double mult = pixels ? 1.0 : 48.0;
    if ((mods & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK) {
        if (dy < 0 && m_zoom != Zoom::Week)   set_zoom(m_zoom == Zoom::Season ? Zoom::Month : Zoom::Week);
        if (dy > 0 && m_zoom != Zoom::Season) set_zoom(m_zoom == Zoom::Week ? Zoom::Month : Zoom::Season);
        return true;
    }
    if ((mods & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK) {
        m_ox += (dy + dx) * mult;
    } else {
        m_ox += dx * mult;
        m_oy += dy * mult;
    }
    clamp_offsets();
    queue_draw();
    return true;
}

void TimelineCanvas::on_motion(double x, double y) {
    if (m_drag != Drag::None) return;
    const Row* r = row_at(x, y);
    if (r != m_hover) {
        m_hover = r;
        set_cursor(r ? "pointer" : "");
        queue_draw();
    }
}

bool TimelineCanvas::on_key(guint key, guint, Gdk::ModifierType mods) {
    const bool ctrl = (mods & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
    if (ctrl) return false;   // Ctrl+Z, Ctrl+F ... belong to the window
    const double vw = view_w(), vh = view_h();
    switch (key) {
    case GDK_KEY_Left:      m_ox -= vw / 4; break;
    case GDK_KEY_Right:     m_ox += vw / 4; break;
    case GDK_KEY_Up:        m_oy -= 60; break;
    case GDK_KEY_Down:      m_oy += 60; break;
    case GDK_KEY_Page_Up:   m_oy -= vh * 0.8; break;
    case GDK_KEY_Page_Down: m_oy += vh * 0.8; break;
    case GDK_KEY_Home:
    case GDK_KEY_t:         go_today(); return true;
    case GDK_KEY_plus:
    case GDK_KEY_equal:
    case GDK_KEY_KP_Add:
        set_zoom(m_zoom == Zoom::Season ? Zoom::Month : Zoom::Week); return true;
    case GDK_KEY_minus:
    case GDK_KEY_KP_Subtract:
        set_zoom(m_zoom == Zoom::Week ? Zoom::Month : Zoom::Season); return true;
    case GDK_KEY_Escape:
        if (m_drag == Drag::Move) { end_move(false); return true; }   // s063: put it back
        m_sig_close.emit(); return true;
    default: return false;
    }
    clamp_offsets();
    queue_draw();
    return true;
}

bool TimelineCanvas::on_tooltip(int x, int y, bool, const Glib::RefPtr<Gtk::Tooltip>& tip) {
    const Clump* c = nullptr;
    const Row* r = row_at(x, y, &c);
    if (r && !r->more) {
        std::string t = Glib::Markup::escape_text(r->title);
        std::string sub;
        if (r->kind == core::TlKind::Project) sub = "Project";
        else if (r->kind == core::TlKind::Note) sub = "Note";
        else sub = r->step ? "Step" : "Todo";
        if (!r->step && c) sub += " · " + why_words(r->why, c->day, m_today);
        if (r->estimate) sub += " · " + core::format_estimate(r->estimate);
        if (r->steps) sub += " · " + std::to_string(r->steps) + (r->steps == 1 ? " step" : " steps") +
                             " (right-click)";
        tip->set_markup("<b>" + t + "</b>\n" + Glib::Markup::escape_text(sub));
        return true;
    }
    if (c && c->compact) {
        std::string t = "<b>" + Glib::Markup::escape_text(c->head) + "</b>";
        for (const auto& row : c->rows)
            t += "\n" + (row.more ? "+" + std::to_string(row.more) + " more"
                                  : std::string(Glib::Markup::escape_text(row.title)));
        t += "\n<small>Right-click to open</small>";
        tip->set_markup(t);
        return true;
    }
    return false;
}

// ── drawing ─────────────────────────────────────────────────────────────────
void TimelineCanvas::on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    if (!m_tl.first) return;
    draw_body(cr, w, h);
    draw_strip(cr, w);
    draw_threads(cr, w);
    if (thumb_shown()) draw_thumb(cr, w, h);
    if (m_drag == Drag::Move) draw_move(cr, w, h);   // s063
    // A slim scroll mark at the right while the body runs past the window.
    if (m_ch > view_h() + 1) {
        GdkRGBA fg;
        gtk_widget_get_color(GTK_WIDGET(gobj()), &fg);
        const double top = strip_h() + 4, span = h - top - 8 - (thumb_shown() ? kThumbH + kThumbM : 0);
        const double bh = std::max(24.0, span * view_h() / m_ch);
        const double by = top + (span - bh) * (m_oy / std::max(1.0, m_ch - view_h()));
        cr->set_source_rgba(fg.red, fg.green, fg.blue, 0.22);
        rounded(cr, w - 7, by, 4, bh, 2);
        cr->fill();
    }
}

// s063: the line in hand -- the day under the pointer lit in the strip, a
// guide down from it, and the line itself (with where it would go) by the
// pointer.
void TimelineCanvas::draw_move(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    GdkRGBA fgc;
    gtk_widget_get_color(GTK_WIDGET(gobj()), &fgc);
    const Gdk::RGBA fg(fgc.red, fgc.green, fgc.blue, 1.0);
    const Gdk::RGBA accent = rgba(appearance::accent_in_force());
    const double ppd = px_per_day();
    if (m_drop_day) {
        const double cx = day_x(m_drop_day) - m_ox;
        // the day, lit
        source(cr, accent, 0.30);
        rounded(cr, cx - std::max(14.0, ppd / 2 - 1), kMonthRow + 2, std::max(28.0, ppd - 2), kDayRow - 4, 8);
        cr->fill();
        // the guide down to the hand
        source(cr, accent, 0.75);
        cr->set_line_width(1.5);
        std::vector<double> dash{4.0, 3.0};
        cr->set_dash(dash, 0);
        cr->move_to(cx + 0.5, strip_h());
        cr->line_to(cx + 0.5, std::min<double>(h, m_py));
        cr->stroke();
        cr->unset_dash();
    }
    // the line itself, and where it would go
    const std::string where = m_drop_day ? fmt(m_drop_day, "%a %e %b") : std::string("not a day");
    auto lay = create_pango_layout(m_move_title + "  \u2192  " + where);
    int lw = 0, lh = 0;
    lay->get_pixel_size(lw, lh);
    double bx = m_px + 14, by = m_py - lh / 2.0 - 6;
    if (bx + lw + 20 > w) bx = m_px - lw - 34;
    by = std::max(strip_h() + 2, by);
    source(cr, fg, 0.10);
    rounded(cr, bx + 1, by + 2, lw + 20, lh + 12, 9);
    cr->fill();
    source(cr, m_drop_day ? accent : fg, m_drop_day ? 0.95 : 0.55);
    rounded(cr, bx, by, lw + 20, lh + 12, 9);
    cr->fill();
    cr->set_source_rgba(1, 1, 1, 1);
    cr->move_to(bx + 10, by + 6);
    lay->show_in_cairo_context(cr);
}

// s064: lines = links. Each linked pair, a curve from the edge of one line to
// the edge of the other; both ends dotted. A closed card's step lends its
// carrier's line. Lit (and thicker) while the pointer is on either end.
void TimelineCanvas::draw_links(const Cairo::RefPtr<Cairo::Context>& cr, int, int) {
    if (m_tl.links.empty()) return;
    struct End { double lx, rx, y; int clump; };
    std::unordered_map<core::NodeId, End> at;
    const double top = strip_h();
    for (std::size_t ci = 0; ci < m_clumps.size(); ++ci) {
        const Clump& c = m_clumps[ci];
        for (std::size_t i = 0; i < c.rows.size(); ++i) {
            const Row& r = c.rows[i];
            if (r.more) continue;
            End e;
            e.clump = static_cast<int>(ci);
            if (c.compact) {
                const double dx = c.x + 10 + (i % 2) * 14, dy = c.y + 22 + (i / 2) * kDot;
                e.lx = dx - 5; e.rx = dx + 5; e.y = dy;
            } else {
                e.lx = c.x + 2; e.rx = c.x + c.w - 2;
                e.y = c.y + kHead + i * kRow + kRow / 2;
            }
            e.lx -= m_ox; e.rx -= m_ox; e.y += top - m_oy;
            if (!r.id.empty()) at.emplace(r.id, e);                  // its own line first ...
            for (const auto& id : r.carries) at.emplace(id, e);     // ... then what it carries
        }
    }
    const Gdk::RGBA accent = rgba(appearance::accent_in_force());
    const core::NodeId hover = m_hover ? m_hover->id : core::NodeId{};
    cr->save();
    cr->rectangle(0, top, get_width(), get_height() - top);
    cr->clip();
    for (const auto& L : m_tl.links) {
        auto ia = at.find(L.a), ib = at.find(L.b);
        if (ia == at.end() || ib == at.end()) continue;
        End a = ia->second, b = ib->second;
        const bool lit = !hover.empty() && (hover == L.a || hover == L.b);
        double x0, y0, x1, y1, c0x, c1x;
        if (a.lx > b.lx) std::swap(a, b);   // a is the one further left
        if (a.clump == b.clump || b.lx < a.rx + 24) {
            // the same card, or one above the other: out to the right and back
            // the same card, or overlapping columns: out past the wider right
            // edge and back, so the curve never crosses a card it joins
            x0 = a.rx; y0 = a.y; x1 = b.rx; y1 = b.y;
            const double edge = std::max(x0, x1);
            const double bulge = 22 + std::min(60.0, std::abs(y1 - y0) * 0.25);
            c0x = edge + bulge; c1x = edge + bulge;
        } else {                               // side by side: edge to edge
            x0 = a.rx; y0 = a.y; x1 = b.lx; y1 = b.y;
            const double dx = std::max(30.0, (x1 - x0) * 0.45);
            c0x = x0 + dx; c1x = x1 - dx;
        }
        source(cr, accent, lit ? 0.95 : 0.45);
        cr->set_line_width(lit ? 2.2 : 1.4);
        cr->move_to(x0, y0);
        cr->curve_to(c0x, y0, c1x, y1, x1, y1);
        cr->stroke();
        for (auto [ex, ey] : {std::pair{x0, y0}, std::pair{x1, y1}}) {
            cr->begin_new_path();
            cr->arc(ex, ey, lit ? 3.2 : 2.4, 0, 2 * M_PI);
            cr->fill();
        }
    }
    cr->restore();
}

void TimelineCanvas::draw_body(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    GdkRGBA fgc;
    gtk_widget_get_color(GTK_WIDGET(gobj()), &fgc);
    const Gdk::RGBA fg(fgc.red, fgc.green, fgc.blue, 1.0);
    const Gdk::RGBA accent = rgba(appearance::accent_in_force());
    const double top = strip_h();
    const double ppd = px_per_day();

    cr->save();
    cr->rectangle(0, top, w, h - top);
    cr->clip();

    // s060: lanes -- every other one a shade off, a hairline between.
    for (std::size_t i = 0; i < m_lanes.size(); ++i) {
        const Lane& L = m_lanes[i];
        if (L.title.empty()) continue;
        const double ly = L.y - m_oy + top;
        if (ly > h || ly + L.h < top) continue;
        if (i % 2) { source(cr, fg, 0.022); cr->rectangle(0, ly, w, L.h); cr->fill(); }
        if (i) { source(cr, fg, 0.12); cr->rectangle(0, std::round(ly), w, 1); cr->fill(); }
    }
    // Weekends, faintly -- a week's rhythm without a grid.
    if (m_zoom != Zoom::Season) {
        const int first = std::max(0, static_cast<int>((m_ox - kLeft) / ppd) - 1);
        const int last  = static_cast<int>((m_ox + w - kLeft) / ppd) + 1;
        const int ndays = core::day_index(m_tl.first, m_tl.last) + 1;
        for (int i = first; i <= std::min(last, ndays - 1); ++i) {
            const int wd = local(core::day_at(m_tl.first, i)).tm_wday;
            if (wd != 0 && wd != 6) continue;
            source(cr, fg, 0.025);
            cr->rectangle(kLeft + i * ppd - m_ox, top, ppd, h - top);
            cr->fill();
        }
    }
    // Someday's ground.
    if (m_someday_x > 0) {
        source(cr, fg, 0.03);
        cr->rectangle(m_someday_x - 20 - m_ox, top, m_cw - m_someday_x + 20, h - top);
        cr->fill();
    }
    // Today, top to bottom.
    {
        const double tx = day_x(m_today) - m_ox;
        source(cr, accent, 0.55);
        cr->set_line_width(1.5);
        cr->move_to(std::round(tx) + 0.5, top);
        cr->line_to(std::round(tx) + 0.5, h);
        cr->stroke();
    }
    // Leaders: each clump hangs from its day.
    const bool finding = !m_match_set.empty();
    cr->set_line_width(1);
    for (const auto& c : m_clumps) {
        if (!c.day) continue;
        const double x = std::round(c.anchor - m_ox) + 0.5;
        if (x < -2 || x > w + 2) continue;
        bool lit = !finding;
        if (finding)
            for (const auto& r : c.rows)
                for (const auto& id : r.carries) lit = lit || m_match_set.count(id);
        double from = top;
        bool named = false;
        if (c.lane >= 0 && c.lane < static_cast<int>(m_lanes.size())) {
            const Lane& L = m_lanes[static_cast<std::size_t>(c.lane)];
            named = !L.title.empty();
            if (named) from = std::max(top, L.body_top - 6 - m_oy + top);
        }
        source(cr, fg, lit ? 0.22 : 0.07);
        cr->move_to(x, from);
        cr->line_to(x, c.y - m_oy + top);
        cr->stroke();
        if (named && from > top) {   // the day, marked where the lane begins
            cr->begin_new_path();
            source(cr, fg, lit ? 0.45 : 0.15);
            cr->arc(x, from, 2.2, 0, 2 * M_PI);
            cr->fill();
        }
    }
    // The clumps.
    for (const auto& c : m_clumps) {
        const double sx = c.x - m_ox, sy = c.y - m_oy + top;
        if (sx > w || sx + c.w < 0 || sy > h || sy + c.h < top - 4) continue;
        draw_clump(cr, c, sx, sy);
    }
    draw_links(cr, w, h);   // s064: over the cards, edge to edge
    // s060: each lane's name and its line, held at the left as you scroll.
    for (const auto& L : m_lanes) {
        if (L.title.empty()) continue;
        const double ly = L.y - m_oy + top;
        if (ly > h || ly + L.h < top) continue;
        auto font = get_pango_context()->get_font_description();
        auto name = create_pango_layout(L.title);
        name->set_font_description(scaled(font, 1.1, Pango::Weight::BOLD));
        auto line = create_pango_layout(L.line);
        line->set_font_description(scaled(font, 0.85));
        int nw = 0, nh = 0, lw = 0, lh = 0;
        name->get_pixel_size(nw, nh);
        line->get_pixel_size(lw, lh);
        const double ny = std::max(ly + 8, top + 4);
        if (ny + nh > ly + L.h - 4) continue;   // its lane has scrolled away under the strip
        // A pill behind it, so a name held over the cards still reads.
        if (appearance::is_dark()) cr->set_source_rgba(0.17, 0.17, 0.18, 0.92);
        else cr->set_source_rgba(0.98, 0.98, 0.98, 0.92);
        rounded(cr, 8, ny - 3, nw + lw + 26, nh + 6, (nh + 6) / 2.0);
        cr->fill();
        source(cr, fg, 0.9);
        cr->move_to(16, ny);
        name->show_in_cairo_context(cr);
        source(cr, fg, 0.55);
        cr->move_to(16 + nw + 10, ny + (nh - lh) / 2.0);
        line->show_in_cairo_context(cr);
    }
    // Empty: say how.
    if (m_clumps.empty()) {
        auto lay = create_pango_layout("Nothing dated yet. Give a todo a due date, or turn on Someday.");
        int lw = 0, lh = 0;
        lay->get_pixel_size(lw, lh);
        source(cr, fg, 0.5);
        cr->move_to((w - lw) / 2.0, top + 60);
        lay->show_in_cairo_context(cr);
    }
    cr->restore();
}

void TimelineCanvas::draw_clump(const Cairo::RefPtr<Cairo::Context>& cr, const Clump& c, double x, double y) {
    GdkRGBA fgc;
    gtk_widget_get_color(GTK_WIDGET(gobj()), &fgc);
    const Gdk::RGBA fg(fgc.red, fgc.green, fgc.blue, 1.0);
    const Gdk::RGBA accent = rgba(appearance::accent_in_force());
    const bool dark = appearance::is_dark();
    const auto sc = appearance::state_colours();
    auto tone = [&](core::TlTone t) -> std::pair<Gdk::RGBA, double> {
        switch (t) {
        case core::TlTone::Late:      return {rgba(sc.late), 1.0};
        case core::TlTone::Today:     return {rgba(sc.today), 1.0};
        case core::TlTone::Flagged:   return {rgba(sc.flagged), 1.0};
        case core::TlTone::Available: return {rgba(sc.available), 1.0};
        case core::TlTone::Done:      return {rgba(sc.done), 1.0};
        case core::TlTone::Waiting:   return {fg, 0.40};
        case core::TlTone::Note:      return {fg, 0.45};
        }
        return {fg, 0.5};
    };

    const bool finding = !m_match_set.empty();
    auto row_lit = [&](const Row& r) {
        for (const auto& id : r.carries) if (m_match_set.count(id)) return true;
        return false;
    };
    bool lit = !finding;
    if (finding) for (const auto& r : c.rows) lit = lit || row_lit(r);
    const core::NodeId cur_match = (m_match_cur >= 0 && m_match_cur < static_cast<int>(m_matches.size()))
                                       ? m_matches[static_cast<std::size_t>(m_match_cur)] : core::NodeId{};

    if (!lit) cr->push_group();

    // Soft shadow, then the frosted card.
    for (int i = 3; i >= 1; --i) {
        cr->set_source_rgba(0, 0, 0, dark ? 0.10 : 0.035);
        rounded(cr, x - i * 0.5, y + i, c.w + i, c.h + i * 0.5, 11);
        cr->fill();
    }
    if (dark) cr->set_source_rgba(1, 1, 1, 0.075);
    else      cr->set_source_rgba(1, 1, 1, 0.93);
    rounded(cr, x, y, c.w, c.h, 10);
    cr->fill_preserve();
    source(cr, fg, c.open ? 0.20 : 0.10);
    cr->set_line_width(1);
    cr->stroke();

    auto font = get_pango_context()->get_font_description();

    if (c.compact) {
        // Season: the day's number of things, then a dot each.
        auto lay = create_pango_layout(std::to_string(c.rows.empty() ? 0 : c.rows.size()));
        int n = 0;
        for (const auto& r : c.rows) n += r.more ? r.more : 1;
        lay->set_text(std::to_string(n));
        Pango::FontDescription f = scaled(font, 3.0 / 4, Pango::Weight::BOLD);
        lay->set_font_description(f);
        int lw = 0, lh = 0;
        lay->get_pixel_size(lw, lh);
        source(cr, fg, 0.55);
        cr->move_to(x + (c.w - lw) / 2, y + 3);
        lay->show_in_cairo_context(cr);
        for (std::size_t i = 0; i < c.rows.size(); ++i) {
            const Row& r = c.rows[i];
            const double dx = x + 10 + (i % 2) * 14, dy = y + 22 + (i / 2) * kDot;
            if (r.more) {
                source(cr, fg, 0.45);
                cr->begin_new_path();
                cr->arc(dx, dy, 1.6, 0, 2 * M_PI);
                cr->fill();
                continue;
            }
            auto [col, a] = tone(r.tone);
            cr->begin_new_path();
            const double rr = std::min(5.0, dot_r(r.estimate));
            source(cr, col, a * ((finding && !row_lit(r)) ? 0.35 : 1.0));
            if (r.kind == core::TlKind::Note) rounded(cr, dx - 3.5, dy - 3.5, 7, 7, 2);
            else cr->arc(dx, dy, rr, 0, 2 * M_PI);
            if (r.why == core::TlWhy::Starts) { cr->set_line_width(1.5); cr->stroke(); }
            else cr->fill();
            if (r.id == m_current || r.id == cur_match) {
                source(cr, accent, 1.0);
                cr->set_line_width(1.5);
                cr->begin_new_path();
                cr->arc(dx, dy, rr + 2.5, 0, 2 * M_PI);
                cr->stroke();
            }
        }
        if (!lit) { cr->pop_group_to_source(); cr->paint_with_alpha(0.22); }
        return;
    }

    // Full: the day, then a line each.
    {
        std::string head = c.head;
        for (auto& ch : head) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        auto lay = create_pango_layout(head);
        Pango::FontDescription f = scaled(font, 0.8, Pango::Weight::BOLD);
        lay->set_font_description(f);
        lay->set_width(static_cast<int>((c.w - 24) * PANGO_SCALE));
        lay->set_ellipsize(Pango::EllipsizeMode::END);
        const bool today = c.day == m_today;
        if (today) source(cr, accent, 1.0); else source(cr, fg, 0.50);
        cr->move_to(x + 12, y + 6);
        lay->show_in_cairo_context(cr);
        if (c.open) {   // a small mark that the clump is open
            source(cr, fg, 0.35);
            cr->move_to(x + c.w - 16, y + 10);
            cr->line_to(x + c.w - 12, y + 14);
            cr->line_to(x + c.w - 8, y + 10);
            cr->set_line_width(1.2);
            cr->stroke();
        }
    }
    for (std::size_t i = 0; i < c.rows.size(); ++i) {
        const Row& r = c.rows[i];
        const double ry = y + kHead + i * kRow;
        const double indent = r.step ? 14 : 0;
        const bool rlit = finding && row_lit(r);
        if (rlit) {
            source(cr, accent, 0.16);
            rounded(cr, x + 4, ry, c.w - 8, kRow - 1, 6);
            cr->fill();
        } else if (&r == m_hover) {
            source(cr, fg, 0.06);
            rounded(cr, x + 4, ry, c.w - 8, kRow - 1, 6);
            cr->fill();
        }
        bool ring = r.id == m_current && !r.more;
        bool cur = false;
        if (!cur_match.empty()) for (const auto& id : r.carries) cur = cur || (id == cur_match && (id == r.id || r.more || r.steps));
        if (ring || (cur && r.id == cur_match)) {
            source(cr, accent, 0.9);
            cr->set_line_width(1.5);
            rounded(cr, x + 4.5, ry + 0.5, c.w - 9, kRow - 2, 6);
            cr->stroke();
        }
        const double dim = (finding && !rlit) ? 0.45 : 1.0;
        if (r.more) {
            auto lay = create_pango_layout("+" + std::to_string(r.more) + " more");
            Pango::FontDescription f = scaled(font, 7.0 / 8);
            lay->set_font_description(f);
            source(cr, fg, 0.55 * dim);
            cr->move_to(x + 28, ry + 2);
            lay->show_in_cairo_context(cr);
            continue;
        }
        // The dot: a ring for a project, a disc for a todo (hollow when it
        // only starts that day), a small square for a note; its size is the
        // estimate.
        auto [col, a] = tone(r.tone);
        const double cx = x + 16 + indent, cy = ry + kRow / 2;
        const double rr = r.step ? std::min(4.0, dot_r(r.estimate)) : dot_r(r.estimate);
        source(cr, col, a * dim);
        cr->begin_new_path();
        if (r.kind == core::TlKind::Project) {
            cr->set_line_width(2);
            cr->arc(cx, cy, rr + 0.5, 0, 2 * M_PI);
            cr->stroke();
            cr->begin_new_path();
            cr->arc(cx, cy, std::max(1.5, rr - 3), 0, 2 * M_PI);
            cr->fill();
        } else if (r.kind == core::TlKind::Note) {
            rounded(cr, cx - 4, cy - 4.5, 8, 9, 2);
            cr->set_line_width(1.3);
            cr->stroke();
        } else if (r.why == core::TlWhy::Starts) {
            cr->set_line_width(1.6);
            cr->arc(cx, cy, rr - 0.5, 0, 2 * M_PI);
            cr->stroke();
        } else {
            cr->arc(cx, cy, rr, 0, 2 * M_PI);
            cr->fill();
        }
        // The words.
        const double tx = x + 28 + indent;
        double right = x + c.w - 10;
        if (r.steps && !c.open) {
            auto badge = create_pango_layout(std::to_string(r.steps));
            Pango::FontDescription f = scaled(font, 3.0 / 4, Pango::Weight::BOLD);
            badge->set_font_description(f);
            int bw = 0, bh = 0;
            badge->get_pixel_size(bw, bh);
            const double pw = bw + 10;
            source(cr, fg, 0.09 * dim);
            rounded(cr, right - pw, cy - 8, pw, 16, 8);
            cr->fill();
            source(cr, fg, 0.6 * dim);
            cr->move_to(right - pw + 5, cy - bh / 2.0);
            badge->show_in_cairo_context(cr);
            right -= pw + 4;
        }
        auto lay = create_pango_layout(r.title);
        Pango::FontDescription f = font;
        if (r.kind == core::TlKind::Project) f.set_weight(Pango::Weight::BOLD);
        if (r.step) f = scaled(f, 0.9);
        lay->set_font_description(f);
        lay->set_width(static_cast<int>(std::max(10.0, right - tx) * PANGO_SCALE));
        lay->set_ellipsize(Pango::EllipsizeMode::END);
        int lw = 0, lh = 0;
        lay->get_pixel_size(lw, lh);
        const bool done = r.tone == core::TlTone::Done;
        const double ta = (done ? 0.5 : (r.kind == core::TlKind::Note ? 0.75 : 0.92)) * dim;
        source(cr, fg, ta);
        cr->move_to(tx, cy - lh / 2.0);
        lay->show_in_cairo_context(cr);
        if (done) {
            cr->set_line_width(1);
            cr->move_to(tx, std::round(cy) + 0.5);
            cr->line_to(tx + lw, std::round(cy) + 0.5);
            cr->stroke();
        }
    }
    if (!lit) { cr->pop_group_to_source(); cr->paint_with_alpha(0.22); }
}

void TimelineCanvas::draw_strip(const Cairo::RefPtr<Cairo::Context>& cr, int w) {
    GdkRGBA fgc;
    gtk_widget_get_color(GTK_WIDGET(gobj()), &fgc);
    const Gdk::RGBA fg(fgc.red, fgc.green, fgc.blue, 1.0);
    const Gdk::RGBA accent = rgba(appearance::accent_in_force());
    const double ppd = px_per_day();
    const double sh = kMonthRow + kDayRow;

    // The strip's ground: a shade off the page, and a hairline under it.
    source(cr, fg, 0.035);
    cr->rectangle(0, 0, w, strip_h());
    cr->fill();
    source(cr, fg, 0.12);
    cr->rectangle(0, strip_h() - 1, w, 1);
    cr->fill();

    auto font = get_pango_context()->get_font_description();
    const int ndays = core::day_index(m_tl.first, m_tl.last) + 1;
    const int i0 = std::max(0, static_cast<int>((m_ox - kLeft) / ppd) - 1);
    const int i1 = std::min(ndays - 1, static_cast<int>((m_ox + w - kLeft) / ppd) + 1);

    // Days with something on them -- a dot under the number, in the colour of
    // the most pressing thing there. A day with a find match gets the accent.
    std::map<std::int64_t, core::TlTone> worst;
    for (const auto& d : m_tl.days) {
        core::TlTone t = core::TlTone::Note;
        for (const auto& it : d.items) {
            if (it.tone == core::TlTone::Late) { t = it.tone; break; }
            if (it.tone == core::TlTone::Today) t = it.tone;
            else if (t == core::TlTone::Note && it.tone != core::TlTone::Note) t = it.tone;
        }
        worst[d.day] = t;
    }
    const auto sc = appearance::state_colours();

    // Months: a name where each begins; the one in view sticks at the left.
    {
        Pango::FontDescription f = font;
        f.set_weight(Pango::Weight::BOLD);
        std::vector<std::pair<double, std::string>> marks;
        std::string sticky;
        for (int i = 0; i < ndays; ++i) {
            const std::int64_t d = core::day_at(m_tl.first, i);
            const std::tm tm = local(d);
            if (tm.tm_mday != 1 && i != 0) continue;
            const double x = kLeft + i * ppd - m_ox;
            const std::string name = fmt(d, "%B %Y");
            if (x <= 8) sticky = name;
            else if (x < w) marks.push_back({x, name});
        }
        double limit = marks.empty() ? w : marks.front().first - 12;
        auto put = [&](double x, const std::string& name, double max_right) {
            auto lay = create_pango_layout(name);
            lay->set_font_description(f);
            int lw = 0, lh = 0;
            lay->get_pixel_size(lw, lh);
            if (x + lw > max_right) return;
            source(cr, fg, 0.85);
            cr->move_to(x, 3);
            lay->show_in_cairo_context(cr);
        };
        if (!sticky.empty()) put(10, sticky, limit);
        for (std::size_t k = 0; k < marks.size(); ++k)
            put(marks[k].first + 4, marks[k].second, k + 1 < marks.size() ? marks[k + 1].first - 8 : w);
    }

    // Days.
    for (int i = i0; i <= i1; ++i) {
        const std::int64_t d = core::day_at(m_tl.first, i);
        const std::tm tm = local(d);
        const double cx = kLeft + (i + 0.5) * ppd - m_ox;
        const bool today = d == m_today;
        // A tick at the start of each day (each week, at Season).
        if (m_zoom != Zoom::Season || tm.tm_wday == 1) {
            source(cr, fg, tm.tm_mday == 1 ? 0.30 : 0.10);
            const double tx = std::round(kLeft + i * ppd - m_ox) + 0.5;
            cr->set_line_width(1);
            cr->move_to(tx, kMonthRow + (m_zoom == Zoom::Season ? 14 : 4));
            cr->line_to(tx, sh - 2);
            cr->stroke();
        }
        std::string label;
        if (m_zoom == Zoom::Week) label = fmt(d, "%a %e");
        else if (m_zoom == Zoom::Month) label = std::to_string(tm.tm_mday);
        else if (tm.tm_wday == 1 || today) label = std::to_string(tm.tm_mday);
        if (!label.empty()) {
            auto lay = create_pango_layout(label);
            Pango::FontDescription f = scaled(font, m_zoom == Zoom::Season ? 4.0 / 5 : 9.0 / 10);
            if (today) f.set_weight(Pango::Weight::BOLD);
            lay->set_font_description(f);
            int lw = 0, lh = 0;
            lay->get_pixel_size(lw, lh);
            const double ly = kMonthRow + (m_zoom == Zoom::Month ? 12 : 6);
            if (today) {
                source(cr, accent, 1.0);
                rounded(cr, cx - lw / 2.0 - 6, ly - 1, lw + 12, lh + 2, (lh + 2) / 2.0);
                cr->fill();
                cr->set_source_rgba(1, 1, 1, 1);
            } else {
                const bool weekend = tm.tm_wday == 0 || tm.tm_wday == 6;
                source(cr, fg, weekend ? 0.45 : 0.75);
            }
            cr->move_to(cx - lw / 2.0, ly);
            lay->show_in_cairo_context(cr);
            if (m_zoom == Zoom::Month) {   // the weekday's initial, small, over it
                auto wl = create_pango_layout(fmt(d, "%a").substr(0, 1));
                Pango::FontDescription wf = scaled(font, 3.0 / 5);
                wl->set_font_description(wf);
                int ww = 0, wh = 0;
                wl->get_pixel_size(ww, wh);
                source(cr, fg, 0.40);
                cr->move_to(cx - ww / 2.0, kMonthRow + 1);
                wl->show_in_cairo_context(cr);
            }
        }
        // Under the number: what the day holds, and whether find found it.
        cr->begin_new_path();
        if (m_match_days.count(d)) {
            source(cr, accent, 1.0);
            cr->arc(cx, sh - 5, 3, 0, 2 * M_PI);
            cr->fill();
        } else if (auto f = worst.find(d); f != worst.end()) {
            Gdk::RGBA col = fg;
            double a = 0.35;
            switch (f->second) {
            case core::TlTone::Late:      col = rgba(sc.late); a = 1; break;
            case core::TlTone::Today:     col = rgba(sc.today); a = 1; break;
            case core::TlTone::Flagged:   col = rgba(sc.flagged); a = 1; break;
            case core::TlTone::Available: col = rgba(sc.available); a = 1; break;
            case core::TlTone::Done:      col = rgba(sc.done); a = 0.8; break;
            default: break;
            }
            source(cr, col, a);
            cr->arc(cx, sh - 5, 2, 0, 2 * M_PI);
            cr->fill();
        }
    }
    // Someday, over its region.
    if (m_someday_x > 0) {
        const double x = m_someday_x - 20 - m_ox;
        if (x < w) {
            auto lay = create_pango_layout("Someday");
            Pango::FontDescription f = font;
            f.set_weight(Pango::Weight::BOLD);
            lay->set_font_description(f);
            source(cr, fg, 0.6);
            cr->move_to(std::max(x + 20, 10.0), kMonthRow + 8);
            lay->show_in_cairo_context(cr);
            source(cr, fg, 0.25);
            cr->rectangle(std::round(x) + 0.5, kMonthRow, 1, kDayRow);
            cr->fill();
        }
    }
}

void TimelineCanvas::draw_threads(const Cairo::RefPtr<Cairo::Context>& cr, int w) {
    if (m_tl.threads.empty()) return;
    GdkRGBA fgc;
    gtk_widget_get_color(GTK_WIDGET(gobj()), &fgc);
    const Gdk::RGBA fg(fgc.red, fgc.green, fgc.blue, 1.0);
    const auto sc = appearance::state_colours();
    const Gdk::RGBA late = rgba(sc.late), avail = rgba(sc.available);
    auto font = get_pango_context()->get_font_description();
    const double y0 = kMonthRow + kDayRow + kThreadGap / 2 + kThreadRow / 2;
    for (std::size_t k = 0; k < std::min(m_tl.threads.size(), kMaxThreads); ++k) {
        const auto& t = m_tl.threads[k];
        const double y = y0 + k * kThreadRow;
        const double x1 = day_x(t.due_day) - m_ox;
        double x0 = day_x(m_today) - m_ox;
        if (!t.beads.empty()) x0 = std::min(x0, day_x(t.beads.front().day) - m_ox);
        // The thread.
        source(cr, t.slipped ? late : fg, t.slipped ? 0.55 : 0.25);
        cr->set_line_width(1.2);
        cr->move_to(x0, y);
        cr->line_to(x1, y);
        cr->stroke();
        // A bead each time a feeder comes round.
        for (const auto& b : t.beads) {
            const double bx = day_x(b.day) - m_ox;
            if (bx < -6 || bx > w + 6) continue;
            source(cr, b.slipped ? late : avail, 1.0);
            cr->begin_new_path();
            cr->arc(bx, y, 3, 0, 2 * M_PI);
            cr->fill();
        }
        // The goal: a diamond on its day, named beside it.
        source(cr, t.slipped ? late : fg, 0.8);
        cr->move_to(x1, y - 4.5);
        cr->line_to(x1 + 4.5, y);
        cr->line_to(x1, y + 4.5);
        cr->line_to(x1 - 4.5, y);
        cr->close_path();
        cr->fill();
        auto lay = create_pango_layout(t.title);
        Pango::FontDescription f = scaled(font, 7.0 / 10);
        lay->set_font_description(f);
        int lw = 0, lh = 0;
        lay->get_pixel_size(lw, lh);
        source(cr, fg, 0.6);
        cr->move_to(x1 + 8, y - lh / 2.0);
        lay->show_in_cairo_context(cr);
    }
}

void TimelineCanvas::draw_thumb(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    GdkRGBA fgc;
    gtk_widget_get_color(GTK_WIDGET(gobj()), &fgc);
    const Gdk::RGBA fg(fgc.red, fgc.green, fgc.blue, 1.0);
    const Gdk::RGBA accent = rgba(appearance::accent_in_force());
    const bool dark = appearance::is_dark();
    const auto t = thumb_rect(w, h);

    for (int i = 4; i >= 1; --i) {
        cr->set_source_rgba(0, 0, 0, dark ? 0.12 : 0.04);
        rounded(cr, t.x - i * 0.5, t.y + i, t.w + i, t.h + i * 0.5, 9);
        cr->fill();
    }
    if (dark) cr->set_source_rgba(0.16, 0.16, 0.17, 0.96);
    else      cr->set_source_rgba(0.99, 0.99, 0.99, 0.96);
    rounded(cr, t.x, t.y, t.w, t.h, 8);
    cr->fill_preserve();
    source(cr, fg, 0.15);
    cr->set_line_width(1);
    cr->stroke();

    cr->save();
    rounded(cr, t.x, t.y, t.w, t.h, 8);
    cr->clip();
    const double sx = t.w / std::max(1.0, m_cw), sy = t.h / std::max(1.0, m_ch);
    // Each clump, in miniature: busy stretches come out darker on their own.
    const bool finding = !m_match_set.empty();
    for (const auto& c : m_clumps) {
        bool lit = false;
        if (finding)
            for (const auto& r : c.rows)
                for (const auto& id : r.carries) lit = lit || m_match_set.count(id);
        if (lit) source(cr, accent, 0.9); else source(cr, fg, 0.28);
        cr->rectangle(t.x + c.x * sx, t.y + c.y * sy, std::max(1.5, c.w * sx), std::max(1.5, c.h * sy));
        cr->fill();
    }
    source(cr, accent, 0.8);
    const double tx = t.x + day_x(m_today) * sx;
    cr->rectangle(std::round(tx), t.y, 1, t.h);
    cr->fill();
    // What is on screen.
    const auto b = core::thumb_box(m_cw, m_ch, t.w, t.h, {m_ox, m_oy, view_w(), view_h()});
    source(cr, accent, 0.14);
    rounded(cr, t.x + b.x, t.y + b.y, std::max(4.0, b.w), std::max(4.0, b.h), 3);
    cr->fill_preserve();
    source(cr, accent, 0.9);
    cr->set_line_width(1.5);
    cr->stroke();
    cr->restore();
}

// ═════════════════════════════════════════════════════════════════════════════
// TimelinePane
// ═════════════════════════════════════════════════════════════════════════════
TimelinePane::TimelinePane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL, 0),
      m_head(widgets::unregistered, "shell.timeline.head", Gtk::Orientation::HORIZONTAL, 12),
      m_title(widgets::unregistered, "shell.timeline.title", "Timeline"),
      m_summary(widgets::unregistered, "shell.timeline.summary"),
      m_find(widgets::unregistered, "shell.timeline.find"),
      m_find_count(widgets::unregistered, "shell.timeline.find_count"),
      m_controls(widgets::unregistered, "shell.timeline.controls", Gtk::Orientation::HORIZONTAL, 6),
      m_week(widgets::unregistered, "shell.timeline.week", "Week"),
      m_month(widgets::unregistered, "shell.timeline.month", "Month"),
      m_season(widgets::unregistered, "shell.timeline.season", "Season"),
      m_g_day(widgets::unregistered, "shell.timeline.group.day", "Day"),
      m_g_place(widgets::unregistered, "shell.timeline.group.place", "Place"),
      m_g_purpose(widgets::unregistered, "shell.timeline.group.purpose", "Purpose"),
      m_chip_projects(widgets::unregistered, "shell.timeline.chip.projects", "Projects"),
      m_chip_todos(widgets::unregistered, "shell.timeline.chip.todos", "Todos"),
      m_chip_notes(widgets::unregistered, "shell.timeline.chip.notes", "Notes"),
      m_chip_someday(widgets::unregistered, "shell.timeline.chip.someday", "Someday"),
      m_chip_links(widgets::unregistered, "shell.timeline.chip.links", "Links"),
      m_today(widgets::unregistered, "shell.timeline.today", "Today") {

    // ── head: the title and what it adds up to; find on the right ──────────
    auto* words = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shell.timeline.head.words",
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

    m_find.set_placeholder_text("Find on the timeline");
    m_find.set_tooltip_text("Words, #tag, is:, due: -- like Find. Enter / Shift+Enter step through");
    m_find.set_size_request(240, -1);
    m_find.set_valign(Gtk::Align::CENTER);
    m_find_count.add_css_class("dim-label");
    m_find_count.set_valign(Gtk::Align::CENTER);
    m_find_count.set_width_chars(9);
    m_find_count.set_xalign(1);
    m_head.append(m_find_count);
    m_head.append(m_find);
    m_head.set_margin_top(12);
    m_head.set_margin_start(18);
    m_head.set_margin_end(14);
    append(m_head);

    // ── controls: zoom, the chips, Today ───────────────────────────────────
    auto* zoom = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shell.timeline.zoom",
                                                 Gtk::Orientation::HORIZONTAL, 0);
    zoom->add_css_class("linked");
    for (auto* b : {&m_week, &m_month, &m_season}) zoom->append(*b);
    m_month.set_group(m_week);
    m_season.set_group(m_week);
    m_week.set_tooltip_text("A week across (Ctrl+scroll, + / - to zoom)");
    m_month.set_tooltip_text("A month or so across");
    m_season.set_tooltip_text("A season across: a dot each");
    m_controls.append(*zoom);

    // s060: Group -- the same cards, in lanes by place or by purpose.
    auto* group = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shell.timeline.group",
                                                  Gtk::Orientation::HORIZONTAL, 0);
    group->add_css_class("linked");
    group->set_margin_start(10);
    for (auto* b : {&m_g_day, &m_g_place, &m_g_purpose}) group->append(*b);
    m_g_place.set_group(m_g_day);
    m_g_purpose.set_group(m_g_day);
    m_g_day.set_active(true);
    m_g_day.set_tooltip_text("One line of days");
    m_g_place.set_tooltip_text("A lane for each place (#at/town ...): what one trip can clear");
    m_g_purpose.set_tooltip_text("A lane for each goal or project: what the work is for");
    for (auto* b : {&m_g_day, &m_g_place, &m_g_purpose})
        b->signal_toggled().connect([this, b]() {
            if (!b->get_active()) return;
            m_canvas.set_group(b == &m_g_day ? core::TlGroup::Day
                               : b == &m_g_place ? core::TlGroup::Place : core::TlGroup::Purpose);
        });
    m_controls.append(*group);

    auto* chips = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shell.timeline.chips",
                                                  Gtk::Orientation::HORIZONTAL, 4);
    chips->set_margin_start(10);
    struct C { widgets::ToggleButton& b; bool on; const char* tip; };
    for (const C& c : {C{m_chip_projects, true, "Projects on their due day"},
                       C{m_chip_todos, true, "Todos on their due day (or start, or the day they were ticked)"},
                       C{m_chip_notes, true, "Notes on the day they were made"},
                       C{m_chip_someday, false, "Undated work, after the last day"},
                       C{m_chip_links, true, "A curve between two things on screen that link to each other"}}) {
        c.b.add_css_class("jot-tl-chip");
        c.b.set_active(c.on);
        c.b.set_tooltip_text(c.tip);
        chips->append(c.b);
    }
    m_controls.append(*chips);
    auto* spacer = Gtk::make_managed<widgets::Box>(widgets::unregistered, "shell.timeline.spacer",
                                                   Gtk::Orientation::HORIZONTAL, 0);
    spacer->set_hexpand(true);
    m_controls.append(*spacer);
    m_today.set_tooltip_text("Back to today (Home)");
    m_controls.append(m_today);
    m_controls.set_margin_start(18);
    m_controls.set_margin_end(14);
    m_controls.set_margin_top(8);
    m_controls.set_margin_bottom(8);
    append(m_controls);
    append(m_canvas);

    sync_zoom_buttons();
    for (auto* b : {&m_week, &m_month, &m_season})
        b->signal_toggled().connect([this, b]() {
            if (m_syncing || !b->get_active()) return;
            m_canvas.set_zoom(b == &m_week ? TimelineCanvas::Zoom::Week
                              : b == &m_month ? TimelineCanvas::Zoom::Month
                                              : TimelineCanvas::Zoom::Season);
        });
    m_canvas.signal_zoomed().connect([this](TimelineCanvas::Zoom) { sync_zoom_buttons(); });
    m_canvas.signal_move().connect(sigc::mem_fun(*this, &TimelinePane::on_move));   // s063
    for (auto* b : {&m_chip_projects, &m_chip_todos, &m_chip_notes, &m_chip_someday, &m_chip_links})
        b->signal_toggled().connect([this]() {
            m_show.projects = m_chip_projects.get_active();
            m_show.todos    = m_chip_todos.get_active();
            m_show.notes    = m_chip_notes.get_active();
            m_show.someday  = m_chip_someday.get_active();
            m_show.links    = m_chip_links.get_active();   // s064
            refresh();
        });
    m_today.signal_clicked().connect([this]() { m_canvas.go_today(); });

    m_find.signal_search_changed().connect([this]() { run_find(true); });
    m_find.signal_activate().connect([this]() { step(+1); });
    m_find.signal_next_match().connect([this]() { step(+1); });
    m_find.signal_previous_match().connect([this]() { step(-1); });
    m_find.signal_stop_search().connect([this]() {
        if (!m_find.get_text().empty()) m_find.set_text("");
        else m_canvas.signal_close().emit();
    });
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint key, guint, Gdk::ModifierType mods) {
            const bool shift = (mods & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
            if ((key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) && shift) { step(-1); return true; }
            if (key == GDK_KEY_Down) { m_canvas.grab_focus(); return true; }
            return false;
        }, false);
    m_find.add_controller(keys);
}

void TimelinePane::sync_zoom_buttons() {
    m_syncing = true;
    m_week.set_active(m_canvas.zoom() == TimelineCanvas::Zoom::Week);
    m_month.set_active(m_canvas.zoom() == TimelineCanvas::Zoom::Month);
    m_season.set_active(m_canvas.zoom() == TimelineCanvas::Zoom::Season);
    m_syncing = false;
}

void TimelinePane::set_source(core::NodeSource* src) {
    m_src = src;
    if (get_mapped()) refresh();
}

void TimelinePane::refresh() {
    if (!m_src) return;
    const std::int64_t now = now_s();
    auto tl = core::build_timeline(*m_src, m_show, now);
    m_summary.set_text(core::timeline_summary(tl, now));
    const std::size_t days = tl.days.size(), threads = tl.threads.size(), someday = tl.someday.size();
    const std::string summary = core::timeline_summary(tl, now);
    m_canvas.set_model(m_src, std::move(tl), now);
    run_find(false);
    if (auto lg = log::get(log::Area::Shell))
        lg->info("timeline: {} days, {} clumps, {} threads, {} someday -- {}", days, m_canvas.clump_count(),
                 threads, someday, summary);
}

// s063: a line dropped on a day. core decides what moves (due, defer, a new
// due); one Ctrl+Z, labelled with the day.
void TimelinePane::on_move(const core::NodeId& id, core::TlWhy why, bool riding, std::int64_t day) {
    if (!m_src) return;
    const core::TlMove mv = core::timeline_move(*m_src, id, why, riding, day);
    const core::Node* n = m_src->find(id);
    if (auto lg = log::get(log::Area::Shell))
        lg->info("timeline: move '{}' to {} -- {}", n ? n->title : id, fmt(day, "%a %e %b"),
                 !mv.ok ? "nothing to change" : mv.defer ? "its start" : "its due");
    if (!mv.ok) return;
    const std::string label = "Move to " + fmt(day, "%a %e %b");
    if (auto* u = dynamic_cast<core::UndoSource*>(m_src))
        core::as_step(*u, label, {id}, false, [&] { core::apply_timeline_move(*u, id, mv); });
    else
        core::apply_timeline_move(*m_src, id, mv);
    refresh();
    m_canvas.set_current(id);
    m_canvas.signal_pick().emit(id);   // Note details follows it, as a click would
}

void TimelinePane::opened() {
    refresh();
    focus_find();
}

void TimelinePane::focus_find() {
    m_find.grab_focus();
    m_find.select_region(0, -1);
}

void TimelinePane::set_current(const core::NodeId& id) { m_canvas.set_current(id); }

void TimelinePane::run_find(bool scroll) {
    if (!m_src) return;
    const std::string q = m_find.get_text();
    auto ids = core::timeline_matches(*m_src, m_canvas.timeline(), q, now_s());
    const core::NodeId first = ids.empty() ? core::NodeId{} : ids.front();
    m_canvas.set_matches(std::move(ids));
    if (scroll && !first.empty()) m_canvas.reveal(first);
    say_count();
    if (scroll && !q.empty())
        if (auto lg = log::get(log::Area::Shell))
            lg->info("timeline: find '{}' -> {} match(es)", q, m_canvas.match_count());
}

void TimelinePane::step(int dir) {
    const int i = m_canvas.step_match(dir);
    if (i < 0) { say_count(); return; }
    const core::NodeId id = m_canvas.match_id(i);
    m_canvas.reveal(id);
    m_canvas.signal_pick().emit(id);
    say_count();
    m_find.grab_focus();   // the pick must not take the keys from the field
    if (auto lg = log::get(log::Area::Shell)) lg->info("timeline: match {} of {}", i + 1, m_canvas.match_count());
}

void TimelinePane::say_count() {
    const std::string q = m_find.get_text();
    const std::size_t n = m_canvas.match_count();
    if (q.empty()) m_find_count.set_text("");
    else if (n == 0) m_find_count.set_text("No match");
    else if (m_canvas.match_cur() >= 0)
        m_find_count.set_text(std::to_string(m_canvas.match_cur() + 1) + " of " + std::to_string(n));
    else m_find_count.set_text(std::to_string(n) + (n == 1 ? " match" : " matches"));
}

}  // namespace jot
