#include "DrawerPane.hpp"
#include "core/Review.hpp"
#include "core/Tags.hpp"

#include <cstdio>
#include <functional>
#include <memory>
#include "Log.hpp"
#include "core/Markdown.hpp"

#include <gdkmm/pixbuf.h>
#include <giomm/contenttype.h>
#include <giomm/menu.h>
#include <giomm/menuitem.h>
#include <giomm/simpleactiongroup.h>
#include <glibmm/variant.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>
#include <glibmm/main.h>
#include <gtkmm/enums.h>
#include <gtkmm/eventcontrollerfocus.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <sstream>

// DrawerPane.cpp -- the third pane. Reads through the NodeSource and the link
// index; writes nothing. Every row it draws is derived, so there is no state
// here that can fall out of step with a note -- only a refresh that can be late.

namespace jot {
namespace {

// A size a human reads. Notes are small, so this tops out well before the units
// get interesting -- but a note with a pasted log in it is a real thing.
std::string human_size(std::uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " bytes";
    const double kb = static_cast<double>(bytes) / 1024.0;
    std::ostringstream o;
    o.precision(1);
    o << std::fixed;
    if (kb < 1024.0) { o << kb << " kB"; return o.str(); }
    o << (kb / 1024.0) << " MB";
    return o.str();
}

// Epoch seconds -> "14 Sep 2026, 21:36". Local time, because the only person
// reading it is sitting in front of the machine that wrote it.
std::string when(std::int64_t epoch) {
    if (epoch <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
    if (!localtime_r(&t, &tm)) return {};
    char buf[64];
    if (!std::strftime(buf, sizeof(buf), "%e %b %Y, %H:%M", &tm)) return {};
    std::string s = buf;
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

// A title that is empty on screen is a note you cannot name. Everywhere the
// drawer prints one, it prints this instead of nothing -- a blank row looks
// like a bug and "Untitled" looks like a note you have not got round to.
std::string titled(const core::Node* n) {
    if (!n) return {};
    return n->title.empty() ? std::string("Untitled") : n->title;
}

// ─────────────────────────────────────────────────────────────────────────────
// THE SECTION TABLE (s016a). Every category the drawer shows is declared here
// and nowhere else: its key (the widget-name stem and the Prefs key), its
// heading, whether it starts open, and whether it hides when empty.
//
// Open by default: everything you can EDIT (Todo, Structure's ordering) and
// everything that hides itself when empty (Links, Linked from, Tags) -- a
// section that is only on screen when it has something to say should not then
// make you click to hear it. Closed by default: File and Identity, which are
// reference you go looking for rather than read in passing.
//
// A new property goes in an existing row's section or gets a row here. If it
// does not fit any category, the category is what is missing.
// ─────────────────────────────────────────────────────────────────────────────
struct SectionSpec {
    const char* key;
    const char* heading;
    bool        default_open;
    bool        hide_empty;
};

constexpr SectionSpec kSections[] = {
    {"project",   "Project",     true,  false},   // s037b
    {"todo",      "Todo",        true,  false},
    {"structure", "Structure",   true,  false},
    {"links",     "Links",       true,  true },
    {"backlinks", "Linked from", true,  true },
    {"tags",      "Tags",        true,  false},   // s035b: always -- it holds the field
    {"enclosures","Enclosures",  true,  true },
    {"file",      "File",        false, false},
    {"identity",  "Identity",    false, false},
};

// The header is a flat Button so it is focusable and Space/Enter fold it, but a
// button brings its own padding, which set the arrows a few pixels in from the
// pinned "Name" caption above them. Trimmed so the column has one left edge.
// Installed once per display, on the pattern TreePane_dnd.cpp's drop CSS set.
void install_section_css() {
    static bool done = false;
    if (done) return;
    auto display = Gdk::Display::get_default();
    if (!display) return;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        ".drawer-section-head { padding: 3px 4px 3px 0; min-height: 0; }"
        // s019: Modified on a linked enclosure. GTK's own theme has no label
        // .warning (libadwaita does); @warning_color is in both.
        ".drawer-modified { color: @warning_color; }"
        // s035: a tag chip -- a link-sized flat button, not a dialog button.
        ".jot-drawer-tag { padding: 1px 4px; min-height: 0; min-width: 0;"
        "  color: rgb(107, 143, 92); font-weight: 600; }"   // the editor's tag colour
        ".jot-drawer-tag-x { padding: 0 4px; min-height: 0; min-width: 0; }");
    gtk_style_context_add_provider_for_display(
        display->gobj(), GTK_STYLE_PROVIDER(css->gobj()),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    done = true;
}

const SectionSpec* spec_for(const std::string& key) {
    for (const auto& sp : kSections)
        if (key == sp.key) return &sp;
    return nullptr;
}

}  // namespace

DrawerPane::DrawerPane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL, 0),
      m_scroll("drawer.scroll"),
      m_column("drawer.column", Gtk::Orientation::VERTICAL, 6),
      m_empty("drawer.empty"),
      m_name_head("drawer.name_head"),
      m_name("drawer.name"),
      m_todo("drawer.is_todo"),
      m_task_body("drawer.task_body", Gtk::Orientation::VERTICAL, 6),
      m_proj_check("drawer.proj_check"),
      m_proj_why("drawer.proj_why"),
      m_proj_body("drawer.proj_body", Gtk::Orientation::VERTICAL, 6),
      m_proj_dates_note("drawer.proj_dates_note"),
      m_when_box("drawer.when_box", Gtk::Orientation::VERTICAL, 6),
      m_done("drawer.done"),
      m_flag("drawer.flag"),
      m_due_row("drawer.due_row", Gtk::Orientation::HORIZONTAL, 8),
      m_due_label("drawer.due_label"),
      m_due("drawer.due"),
      m_defer_row("drawer.defer_row", Gtk::Orientation::HORIZONTAL, 8),
      m_defer_label("drawer.defer_label"),
      m_defer("drawer.defer"),
      m_repeat_row("drawer.repeat_row", Gtk::Orientation::HORIZONTAL, 8),
      m_repeat_label("drawer.repeat_label"),
      m_repeat("drawer.repeat"),
      m_repeat_done("drawer.repeat_done"),
      m_est_row("drawer.est_row", Gtk::Orientation::HORIZONTAL, 8),
      m_est_label("drawer.est_label"),
      m_est("drawer.est"),
      m_avail("drawer.avail"),
      m_order_row("drawer.order_row", Gtk::Orientation::VERTICAL, 2),
      m_order_label("drawer.order_label"),
      m_order_buttons("drawer.order_buttons", Gtk::Orientation::HORIZONTAL, 0),
      m_order_none("drawer.order_none"),
      m_order_seq("drawer.order_seq"),
      m_order_par("drawer.order_par"),
      m_order_list("drawer.order_list"),
      m_order_says("drawer.order_says"),
      m_state_row("drawer.state_row", Gtk::Orientation::VERTICAL, 2),
      m_state_label("drawer.state_label"),
      m_state_buttons("drawer.state_buttons", Gtk::Orientation::HORIZONTAL, 0),
      m_state_active("drawer.state_active"),
      m_state_hold("drawer.state_hold"),
      m_state_done("drawer.state_done"),
      m_state_drop("drawer.state_drop"),
      m_state_says("drawer.state_says"),
      m_review_row("drawer.review_row", Gtk::Orientation::VERTICAL, 2),
      m_review_label("drawer.review_label"),
      m_review_line("drawer.review_line", Gtk::Orientation::HORIZONTAL, 6),
      m_review_every("drawer.review_every"),
      m_review_btn("drawer.review_btn"),
      m_review_says("drawer.review_says"),
      m_uuid("drawer.uuid"),
      m_copy_link("drawer.copy_link") {
    m_column.set_margin(14);

    m_empty.set_wrap(true);
    m_empty.set_xalign(0.0f);
    m_empty.add_css_class("dim-label");
    m_empty.set_text("No note selected.");
    m_column.append(m_empty);

    // ── Name ────────────────────────────────────────────────────────────────
    // First, above everything, because it is the only thing in this pane you
    // can CHANGE. Everything below is a report; this is a control, and mixing
    // the two without saying which is which is how a panel stops being
    // readable at a glance.
    m_name_head.set_text("Name");
    m_name_head.set_xalign(0.0f);
    m_name_head.add_css_class("dim-label");
    m_name_head.add_css_class("caption-heading");
    m_column.append(m_name_head);

    m_name.set_placeholder_text("Untitled");
    m_name.signal_changed().connect([this]() { write_title(); });
    m_name.set_margin_bottom(6);
    m_column.append(m_name);

    // ── The sections, in display order ──────────────────────────────────────
    // Built before their contents, because the task block and the ordering
    // radios are HELD widgets that go into a section's body rather than into
    // the column.
    m_project_sec = add_section("project");   // s037b: above Todo -- the bigger idea first
    m_todo_sec  = add_section("todo");
    m_structure = add_section("structure");
    m_links     = add_section("links");
    m_backlinks = add_section("backlinks");
    m_tags      = add_section("tags");
    m_enclosures = add_section("enclosures");
    m_file      = add_section("file");
    m_identity  = add_section("identity");
    m_all = {&m_project_sec, &m_todo_sec, &m_structure,  &m_links, &m_backlinks,
             &m_tags,     &m_enclosures, &m_file,  &m_identity};

    build_task_block();
    build_tag_field();   // s035b

    // ── Identity: folded away, not removed ──────────────────────────────────
    // The uuid is a developer's correlation key -- it matches a line in the log
    // and a filename in notes/. That is a real need and a rare one, so its
    // section starts closed rather than showing a line of hex under every note.
    // The parent id is NOT here, in any state: the parent's title is up in
    // Structure, and the tree already shows where you are.
    m_uuid.set_xalign(0.0f);
    m_uuid.set_selectable(true);          // the whole point is copying it
    m_uuid.set_wrap(true);
    m_uuid.add_css_class("monospace");
    m_uuid.add_css_class("caption");
    m_identity.body->append(m_uuid);

    // ── Copy link ───────────────────────────────────────────────────────────
    // The better answer to "I need the uuid". Nobody wants the id; they want a
    // link, and this produces the whole `[Title](jot:<id>)` ready to paste into
    // another note. Showing an id to copy by hand was always the long way round.
    m_copy_link.set_label("Copy link to this note");
    m_copy_link.set_tooltip_text("Puts [Title](jot:\u2026) on the clipboard, "
                                 "ready to paste into another note");
    m_copy_link.signal_clicked().connect([this]() {
        if (!m_id.empty()) m_sig_copy_link.emit(m_id);
    });
    // Pinned below the sections, not inside one: it is an ACTION on the note,
    // and putting it in Links would hide it on every note that has none yet --
    // which is exactly the note you are about to link to.
    m_copy_link.set_margin_top(10);
    m_copy_link.set_halign(Gtk::Align::START);
    m_column.append(m_copy_link);

    // ── enclosure actions (s017) ────────────────────────────────────────────
    // One action per verb, parameterised by the file's NAME, on a group the
    // drawer owns ("encl.*"). The rows are rebuilt on every refresh; the
    // actions are not, so a menu open while the note changes still points at
    // something that exists. The drawer only says what was asked for.
    {
        auto group = Gio::SimpleActionGroup::create();
        for (const char* verb : {"open", "reveal", "copy", "save", "relink", "accept", "embed", "link"}) {
            const std::string v = verb;
            group->add_action_with_parameter(
                v, Glib::VARIANT_TYPE_STRING, [this, v](const Glib::VariantBase& p) {
                    const auto name =
                        Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(p).get();
                    if (auto lg = log::get(log::Area::Drawer))
                        lg->info("enclosure {} '{}'", v, std::string(name));
                    m_sig_enclosure.emit(v, name);
                });
        }
        insert_action_group("encl", group);
    }

    m_scroll.set_child(m_column);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    append(m_scroll);

    show_node("");
}

// ─────────────────────────────────────────────────────────────────────────────
// build_task_block -- where a note becomes a todo.
//
// One checkbox promotes the note; everything else appears underneath it and
// only then. A due date field on a note that is not a task is an offer to fill
// in something that will never be read.
//
// Every control writes through the NodeSource. None of them writes to the other
// controls, and none of them touches the tree or Today: they all find out the
// same way, from the model's change notification, which is the same arrangement
// the three rename surfaces settled into in s006.
// ─────────────────────────────────────────────────────────────────────────────
void DrawerPane::build_task_block() {
    m_todo.set_label("This is a todo");
    m_todo.signal_toggled().connect([this]() {
        if (m_loading || !m_src || m_id.empty()) return;
        m_src->make_task(m_id, m_todo.get_active());
    });
    m_todo_sec.body->append(m_todo);

    m_done.set_label("Done");
    m_done.signal_toggled().connect([this]() {
        if (m_loading || !m_src || m_id.empty()) return;
        m_src->set_done(m_id, m_done.get_active());
    });
    m_task_body.append(m_done);

    m_flag.set_label("Flagged");
    m_flag.set_tooltip_text("This one, today. There is no priority number on "
                            "purpose \u2014 drag it up the list instead.");
    m_flag.signal_toggled().connect([this]() {
        if (m_loading || !m_src || m_id.empty()) return;
        m_src->set_flagged(m_id, m_flag.get_active());
    });
    m_when_box.append(m_flag);
    m_task_body.append(m_when_box);   // s037b: re-homed to the project body when it is not a todo

    // Two date fields, same shape. Enter commits and so does leaving the field,
    // for the reason the inline rename commits on focus-leave: typing a date
    // and clicking away means you meant the date.
    auto date_field = [this](widgets::Box& row, widgets::Label& label,
                             widgets::Entry& entry, const char* text,
                             const char* tip, core::DateKind kind) {
        label.set_text(text);
        label.set_xalign(0.0f);
        label.set_width_chars(6);
        label.add_css_class("dim-label");
        row.append(label);
        entry.set_placeholder_text("none");
        entry.set_tooltip_text(tip);
        entry.set_hexpand(true);
        entry.signal_activate().connect([this, kind]() { commit_date(kind); });
        auto focus = Gtk::EventControllerFocus::create();
        focus->signal_leave().connect([this, kind]() { commit_date(kind); });
        entry.add_controller(focus);
        // s033b: the entry and its calendar button read as one control.
        auto* combo = Gtk::make_managed<widgets::Box>(
            widgets::unregistered, std::string(text) == "Due" ? "drawer.due_combo" : "drawer.defer_combo",
            Gtk::Orientation::HORIZONTAL, 0);
        combo->add_css_class("linked");
        combo->set_hexpand(true);
        combo->append(entry);
        combo->append(*date_picker(kind));
        row.append(*combo);
        m_when_box.append(row);
    };
    date_field(m_due_row, m_due_label, m_due, "Due",
               "YYYY-MM-DD, YYYY-MM-DD HH:MM, today, tomorrow. "
               "A bare date means the end of that day.", core::DateKind::Due);
    date_field(m_defer_row, m_defer_label, m_defer, "Defer",
               "Do not surface it before this. A bare date means the start of "
               "that day.", core::DateKind::Defer);

    // s033. Repeat: typed, like the dates -- "weekly", "every 2 weeks",
    // "every other month". Ticking a repeating todo logs this time and brings
    // the same note back with its dates moved on (core/Repeat).
    m_repeat_label.set_text("Repeat");
    m_repeat_label.set_xalign(0.0f);
    m_repeat_label.set_width_chars(6);
    m_repeat_label.add_css_class("dim-label");
    m_repeat_row.append(m_repeat_label);
    m_repeat.set_placeholder_text("never");
    m_repeat.set_tooltip_text("daily, weekly, monthly, yearly, every 3 days, "
                              "every other week. Empty for never.");
    m_repeat.set_hexpand(true);
    m_repeat.signal_activate().connect([this]() { commit_repeat(); });
    {
        auto focus = Gtk::EventControllerFocus::create();
        focus->signal_leave().connect([this]() { commit_repeat(); });
        m_repeat.add_controller(focus);
    }
    {
        auto* combo = Gtk::make_managed<widgets::Box>(widgets::unregistered, "drawer.repeat_combo",
                                                      Gtk::Orientation::HORIZONTAL, 0);
        combo->add_css_class("linked");
        combo->set_hexpand(true);
        combo->append(m_repeat);
        combo->append(*repeat_picker());
        m_repeat_row.append(*combo);
    }
    // s040. Time: how long it takes. Typed like the dates ("15m", "1h30"),
    // with the usual sizes one click away. Before Repeat: a todo's size is
    // asked about more often than its rhythm.
    m_est_label.set_text("Time");
    m_est_label.set_xalign(0.0f);
    m_est_label.set_width_chars(6);
    m_est_label.add_css_class("dim-label");
    m_est_row.append(m_est_label);
    m_est.set_placeholder_text("how long?");
    m_est.set_tooltip_text("How long it takes: 15m, 1h, 1h30, 90. Empty for no estimate. "
                           "Find › Time (est:30) shows what fits.");
    m_est.set_hexpand(true);
    m_est.signal_activate().connect([this]() { commit_estimate(); });
    {
        auto focus = Gtk::EventControllerFocus::create();
        focus->signal_leave().connect([this]() { commit_estimate(); });
        m_est.add_controller(focus);
        auto* combo = Gtk::make_managed<widgets::Box>(widgets::unregistered, "drawer.est_combo",
                                                      Gtk::Orientation::HORIZONTAL, 0);
        combo->add_css_class("linked");
        combo->set_hexpand(true);
        combo->append(m_est);
        combo->append(*estimate_picker());
        m_est_row.append(*combo);
    }
    m_task_body.append(m_est_row);
    m_task_body.append(m_repeat_row);
    m_repeat_done.set_label("Count from when it is done");
    m_repeat_done.set_tooltip_text("Off: the next one is due an interval after the last DUE "
                                   "date (rent). On: an interval after the day you did it "
                                   "(watering the plants).");
    m_repeat_done.signal_toggled().connect([this]() {
        if (m_loading || !m_src || m_id.empty()) return;
        const core::Node* n = m_src->find(m_id);
        if (!n) return;
        core::Repeat r = n->task.repeat;
        r.from_done = m_repeat_done.get_active();
        m_src->set_repeat(m_id, r);
    });
    m_task_body.append(m_repeat_done);

    // THE DERIVED LINE. Not a control and not stored anywhere -- it is the
    // answer availability() gives about this node right now. It is here because
    // "why is this not in Today" is otherwise an unanswerable question, and an
    // unanswerable question about a hidden task is how you stop trusting the
    // report.
    m_avail.set_xalign(0.0f);
    m_avail.set_wrap(true);
    m_avail.add_css_class("dim-label");
    m_avail.add_css_class("caption");
    m_task_body.append(m_avail);

    m_todo_sec.body->append(m_task_body);

    // ── Children (in the Structure section) ─────────────────────────────────
    // Sequential vs Parallel is the whole GTD engine, and it is a statement
    // about the CHILDREN, so it lives on the parent and not on any of them.
    //
    // s034 (Scott: "one concept, one widget -- and the widget shows the
    // state"): joined icon toggles in one group, exactly one pressed, drawn as
    // what they mean -- an empty box, steps going down, bars side by side,
    // loose squares. The radio text did a job the icons cannot do alone, so
    // the pressed one's meaning is the line under the row: readable without
    // opening anything, which is why it was never a dropdown.
    m_order_label.set_text("Children");
    m_order_label.set_xalign(0.0f);
    m_order_label.add_css_class("dim-label");
    m_order_label.add_css_class("caption-heading");
    m_order_row.append(m_order_label);

    struct Pick { widgets::ToggleButton& b; const char* icon; const char* says; };
    const auto says_line = [](widgets::Label& l) {
        l.set_xalign(0.0f);
        l.set_wrap(true);
        l.add_css_class("dim-label");
        l.add_css_class("caption");
    };
    // Each button writes the model when pressed by a person (not while the
    // pane is filling) and always updates the line, so the line is right
    // after a fill as well as after a click.
    const auto joined = [](widgets::Box& box, widgets::Label& says,
                           std::initializer_list<Pick> picks,
                           std::function<void(int)> write) {
        box.add_css_class("linked");
        box.set_halign(Gtk::Align::START);
        widgets::ToggleButton* first = nullptr;
        int i = 0;
        for (const Pick& p : picks) {
            p.b.set_icon_name(p.icon);
            p.b.set_tooltip_text(p.says);
            if (first) p.b.set_group(*first); else first = &p.b;
            const std::string line = p.says;
            p.b.signal_toggled().connect([&b = p.b, &says, line, write, i]() {
                if (!b.get_active()) return;
                says.set_text(line);
                write(i);
            });
            box.append(p.b);
            ++i;
        }
    };

    joined(m_order_buttons, m_order_says,
           {{m_order_none, "jot-order-none-symbolic",       "Unordered — no statement about the children"},
            {m_order_seq,  "jot-order-sequential-symbolic", "Sequential — one at a time, in order"},
            {m_order_par,  "jot-order-parallel-symbolic",   "Parallel — all available at once"},
            {m_order_list, "jot-order-single-symbolic",     "Single actions — loose todos, no order, no end"}},
           [this](int i) {
               static constexpr core::Status st[] = {core::Status::None, core::Status::Sequential,
                                                     core::Status::Parallel, core::Status::SingleActions};
               if (m_loading || !m_src || m_id.empty()) return;
               m_src->set_status(m_id, st[i]);
           });
    says_line(m_order_says);
    m_order_row.append(m_order_buttons);
    m_order_row.append(m_order_says);
    m_order_row.set_margin_top(6);
    m_structure.body->append(m_order_row);   // after the rebuilt rows, held

    // ── Status (s031) ───────────────────────────────────────────────────────
    // core::set_project_state is the one writer, so "Completed" on a todo is
    // its tick and the two can never disagree. s034: joined, as Children, with
    // the marks the tree draws -- pause for held, the strike for completed,
    // no-entry for dropped (the tree now draws these same jot icons).
    m_state_label.set_text("Status");
    m_state_label.set_xalign(0.0f);
    m_state_label.add_css_class("dim-label");
    m_state_label.add_css_class("caption-heading");
    m_state_row.append(m_state_label);
    joined(m_state_buttons, m_state_says,
           {{m_state_active, "jot-state-active-symbolic",  "Active — its steps are offered"},
            {m_state_hold,   "jot-state-hold-symbolic",    "On hold — nothing in it is offered"},
            {m_state_done,   "jot-state-done-symbolic",    "Completed — everything in it is done"},
            {m_state_drop,   "jot-state-dropped-symbolic", "Dropped — kept, but out of every list"}},
           [this](int i) {
               static constexpr core::ProjectState st[] = {
                   core::ProjectState::Active, core::ProjectState::OnHold,
                   core::ProjectState::Completed, core::ProjectState::Dropped};
               if (m_loading || !m_src || m_id.empty()) return;
               core::set_project_state(*m_src, m_id, st[i]);
           });
    says_line(m_state_says);
    m_state_row.append(m_state_buttons);
    m_state_row.append(m_state_says);
    m_state_row.set_margin_top(6);

    // ── Project (s037b) ─────────────────────────────────────────────────────
    // Said on purpose. Unticking an inferred project stores Off (a folder that
    // holds todos and must never ask for a review); ticking stores On (a
    // reference project of info notes). "Automatic" is the untouched state.
    m_proj_check.set_label("This is a project");
    m_proj_check.set_tooltip_text("A project shows in the Projects tab and comes up for review. "
                                  "Its due and defer dates and its flag pass down to the todos in it.");
    m_proj_check.signal_toggled().connect([this]() {
        if (m_loading || !m_src || m_id.empty()) return;
        core::set_project_mark(*m_src, m_id, m_proj_check.get_active() ? core::ProjectMark::On
                                                                       : core::ProjectMark::Off);
    });
    m_project_sec.body->append(m_proj_check);
    says_line(m_proj_why);
    m_project_sec.body->append(m_proj_why);
    says_line(m_proj_dates_note);
    m_proj_dates_note.set_text("Its due, defer and flag are in Todo, below.");
    m_proj_body.append(m_proj_dates_note);
    m_proj_body.append(m_state_row);
    m_project_sec.body->append(m_proj_body);

    // ── Review (s037) ───────────────────────────────────────────────────────
    // One concept on one line: how often, and "I just looked". The when-line
    // under it is core::review_when -- the same words the Projects tab uses.
    m_review_label.set_text("Review");
    m_review_label.set_xalign(0.0f);
    m_review_label.add_css_class("dim-label");
    m_review_label.add_css_class("caption-heading");
    m_review_row.append(m_review_label);
    m_review_every.set_placeholder_text("weekly");
    m_review_every.set_tooltip_text("How often this project wants a look: every week (empty), "
                                    "every 2 weeks, monthly, every 3 days.");
    m_review_every.set_hexpand(true);
    m_review_every.signal_activate().connect([this]() { commit_review(); });
    {
        auto focus = Gtk::EventControllerFocus::create();
        focus->signal_leave().connect([this]() { commit_review(); });
        m_review_every.add_controller(focus);
    }
    {
        auto* combo = Gtk::make_managed<widgets::Box>(widgets::unregistered, "drawer.review_combo",
                                                      Gtk::Orientation::HORIZONTAL, 0);
        combo->add_css_class("linked");
        combo->set_hexpand(true);
        combo->append(m_review_every);
        combo->append(*review_picker());
        m_review_line.append(*combo);
    }
    m_review_btn.set_icon_name("object-select-symbolic");
    m_review_btn.set_tooltip_text("Reviewed: it has had its look; the clock starts again "
                                  "(Ctrl+Shift+R)");
    m_review_btn.signal_clicked().connect([this]() {
        if (!m_loading) m_sig_reviewed.emit();
    });
    m_review_line.append(m_review_btn);
    m_review_row.append(m_review_line);
    says_line(m_review_says);
    m_review_row.append(m_review_says);
    m_review_row.set_margin_top(6);
    m_proj_body.append(m_review_row);
}

// s037b. Flag / Due / Defer go where the note's dates belong: with the todo
// when it is one (after Done, where they always were), else at the top of the
// project body. Moved only when the home changes, so typing is never disturbed.
void DrawerPane::place_when_box(bool in_task) {
    Gtk::Box& home = in_task ? static_cast<Gtk::Box&>(m_task_body) : static_cast<Gtk::Box&>(m_proj_body);
    if (m_when_box.get_parent() == &home) return;
    if (auto* old = dynamic_cast<Gtk::Box*>(m_when_box.get_parent())) old->remove(m_when_box);
    if (in_task) m_task_body.insert_child_after(m_when_box, m_done);
    else         m_proj_body.prepend(m_when_box);
}

// s037. The Review interval's quick picks, on the Repeat list's pattern.
Gtk::Widget* DrawerPane::review_picker() {
    auto* mb = Gtk::make_managed<widgets::MenuButton>(widgets::unregistered, "drawer.review_pick");
    mb->set_icon_name("pan-down-symbolic");
    mb->set_tooltip_text("Pick how often");
    auto* pop = Gtk::make_managed<widgets::Popover>(widgets::unregistered, "drawer.review_pick.popover");
    auto* col = Gtk::make_managed<widgets::Box>(widgets::unregistered, "drawer.review_pick.column",
                                                Gtk::Orientation::VERTICAL, 0);
    col->set_margin(4);
    struct P { const char* label; const char* text; };
    const P picks[] = {
        {"Every week", ""},  {"Every 2 weeks", "every 2 weeks"}, {"Every month", "every month"},
        {"Every 3 months", "every 3 months"}, {"Every year", "every year"},
    };
    int i = 0;
    for (const auto& p : picks) {
        auto* b = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                     "drawer.review_pick.p" + std::to_string(i));
        b->set_has_frame(false);
        auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                    "drawer.review_pick.l" + std::to_string(i));
        ++i;
        l->set_text(p.label);
        l->set_xalign(0.0f);
        b->set_child(*l);
        const std::string text = p.text;
        b->signal_clicked().connect([this, text, pop]() {
            m_review_every.set_text(text);
            commit_review();
            pop->popdown();
        });
        col->append(*b);
    }
    pop->set_child(*col);
    mb->set_popover(*pop);
    return mb;
}

// s037. Same contract as Repeat: a rule that does not parse turns the field
// red and changes nothing. Empty (or "weekly") is the default.
void DrawerPane::commit_review() {
    if (m_loading || !m_src || m_id.empty()) return;
    const core::Node* n = m_src->find(m_id);
    if (!n) return;
    core::Repeat r;
    if (!core::repeat_parse(std::string(m_review_every.get_text()), r)) {
        m_review_every.add_css_class("error");
        if (auto lg = log::get(log::Area::Drawer))
            lg->info("review '{}' on {}: refused (unparseable)",
                     std::string(m_review_every.get_text()), m_id);
        return;
    }
    m_review_every.remove_css_class("error");
    core::set_review_every(*m_src, m_id, r);   // a no-op write changes nothing
}

// An entry -> the model. A field that does not parse is REFUSED and says so by
// turning red, and the old value stays: silently storing 0 for "next thursday"
// would drop a date the user thinks they set, which is the same class of
// failure as a task wrongly hidden.
void DrawerPane::commit_date(core::DateKind kind) {
    if (m_loading || !m_src || m_id.empty()) return;
    widgets::Entry& e = (kind == core::DateKind::Due) ? m_due : m_defer;
    const std::string text = std::string(e.get_text());
    const core::Node* n = m_src->find(m_id);
    if (!n) return;

    if (!core::date_parses(text)) {
        e.add_css_class("error");
        if (auto lg = log::get(log::Area::Drawer))
            lg->info("date '{}' on {}: refused (unparseable)", text, m_id);
        return;
    }
    e.remove_css_class("error");
    const std::int64_t when =
        core::parse_date(text, kind, static_cast<std::int64_t>(std::time(nullptr)));
    const bool changed = (kind == core::DateKind::Due) ? (n->task.due != when)
                                                       : (n->task.defer != when);
    if (!changed) return;                      // do not bump `modified` for a re-read
    if (kind == core::DateKind::Due) m_src->set_due(m_id, when);
    else                             m_src->set_defer(m_id, when);
}

// ─────────────────────────────────────────────────────────────────────────────
// s033b -- the dropdowns (Scott: "a dropdown that has a calendar widget").
//
// A calendar button beside Due and Defer, a list button beside Repeat. Each
// pick WRITES TEXT INTO THE FIELD and commits it, exactly as typing and Enter
// would: one road to the model, one parse, one set of rules (a bare due is
// the end of its day, a bare defer the start). The typed field stays -- it is
// faster than any picker for "tomorrow", and it is where a time goes.
//
// Due and Defer get the same calendar but different quick picks, because they
// answer different questions: Due is "when must it be done" (Today, Tomorrow,
// Next week); Defer is "when do I want to SEE it" (Tomorrow, This weekend,
// Next Monday, In a week, and 2 days before it is due).
// ─────────────────────────────────────────────────────────────────────────────
Gtk::Widget* DrawerPane::date_picker(core::DateKind kind) {
    const bool due = (kind == core::DateKind::Due);
    const std::string base = due ? "drawer.due_pick" : "drawer.defer_pick";
    widgets::Entry& entry = due ? m_due : m_defer;

    auto* mb = Gtk::make_managed<widgets::MenuButton>(widgets::unregistered, base);
    mb->set_icon_name("x-office-calendar-symbolic");
    mb->set_tooltip_text(due ? "Pick a due date" : "Pick a defer date");
    auto* pop = Gtk::make_managed<widgets::Popover>(widgets::unregistered, base + ".popover");
    auto* col = Gtk::make_managed<widgets::Box>(widgets::unregistered, base + ".column",
                                                Gtk::Orientation::VERTICAL, 6);
    col->set_margin(6);

    // Writes the day into the field, keeping a clock time the field already had.
    auto pick = [this, kind, &entry, pop](const std::string& day) {
        std::string text = day;
        if (!day.empty()) {
            const std::string cur = std::string(entry.get_text());
            const auto sp = cur.find(' ');
            if (sp != std::string::npos && core::date_parses(cur)) text += cur.substr(sp);
        }
        entry.set_text(text);
        commit_date(kind);
        pop->popdown();
    };

    struct Q { const char* label; core::QuickDate q; };
    std::vector<Q> quick = due
        ? std::vector<Q>{{"Today", core::QuickDate::Today},
                         {"Tomorrow", core::QuickDate::Tomorrow},
                         {"Next week", core::QuickDate::NextWeek}}
        : std::vector<Q>{{"Tomorrow", core::QuickDate::Tomorrow},
                         {"This weekend", core::QuickDate::ThisWeekend},
                         {"Next Monday", core::QuickDate::NextMonday},
                         {"In a week", core::QuickDate::NextWeek},
                         {"2 days before due", core::QuickDate::BeforeDue}};
    Gtk::Button* before_due = nullptr;
    widgets::Box* line = nullptr;
    for (std::size_t i = 0; i < quick.size(); ++i) {
        if (i % 3 == 0) {
            line = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                   base + ".quick" + std::to_string(i / 3),
                                                   Gtk::Orientation::HORIZONTAL, 4);
            line->set_homogeneous(true);
            col->append(*line);
        }
        auto* b = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                     base + ".q" + std::to_string(i));
        b->set_label(quick[i].label);
        const core::QuickDate q = quick[i].q;
        b->signal_clicked().connect([this, pick, q]() {
            const core::Node* n = m_src ? m_src->find(m_id) : nullptr;
            const std::string t = core::quick_date_text(
                q, static_cast<std::int64_t>(std::time(nullptr)), n ? n->task.due : 0);
            if (!t.empty()) pick(t);
        });
        if (q == core::QuickDate::BeforeDue) before_due = b;
        line->append(*b);
    }

    auto* cal = Gtk::make_managed<widgets::Calendar>(widgets::unregistered, base + ".calendar");
    col->append(*cal);

    auto* clear = Gtk::make_managed<widgets::Button>(widgets::unregistered, base + ".clear");
    clear->set_label("No date");
    clear->set_has_frame(false);
    clear->signal_clicked().connect([pick]() { pick(""); });
    col->append(*clear);

    pop->set_child(*col);
    mb->set_popover(*pop);

    // Paging the calendar to another month also "selects a day" in GTK 4, so
    // a selection only counts as a PICK when the month did not change with
    // it. The shared state lives with the lambdas, not in the class.
    auto shown = std::make_shared<std::pair<int, int>>(0, 0);   // year, month shown
    auto syncing = std::make_shared<bool>(false);
    pop->signal_show().connect([this, cal, shown, syncing, &entry, before_due, kind]() {
        const std::string cur = std::string(entry.get_text());
        std::int64_t when = core::date_parses(cur)
            ? core::parse_date(cur, kind, static_cast<std::int64_t>(std::time(nullptr))) : 0;
        if (when == 0) when = static_cast<std::int64_t>(std::time(nullptr));
        const auto dt = Glib::DateTime::create_now_local(static_cast<gint64>(when));
        *syncing = true;
        cal->select_day(dt);
        *syncing = false;
        *shown = {dt.get_year(), dt.get_month()};
        if (before_due) {
            const core::Node* n = m_src ? m_src->find(m_id) : nullptr;
            before_due->set_sensitive(n && n->task.due != 0);
        }
    });
    // Whether paging emits day-selected differs between GTK releases (4.14,
    // here, does not), so the page buttons say which month is showing too.
    // Either order of the two signals leaves `shown` right.
    auto paged = [cal, shown]() {
        const Glib::DateTime d = cal->get_date();
        *shown = {d.get_year(), d.get_month()};
    };
    cal->signal_next_month().connect(paged);
    cal->signal_prev_month().connect(paged);
    cal->signal_next_year().connect(paged);
    cal->signal_prev_year().connect(paged);
    cal->signal_day_selected().connect([cal, shown, syncing, pick]() {
        if (*syncing) return;
        const Glib::DateTime d = cal->get_date();
        if (std::make_pair(d.get_year(), d.get_month()) != *shown) {
            *shown = {d.get_year(), d.get_month()};    // paged, not picked
            return;
        }
        char buf[16];
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", d.get_year(), d.get_month(),
                      d.get_day_of_month());
        pick(buf);
    });
    return mb;
}

Gtk::Widget* DrawerPane::repeat_picker() {
    auto* mb = Gtk::make_managed<widgets::MenuButton>(widgets::unregistered, "drawer.repeat_pick");
    mb->set_icon_name("pan-down-symbolic");
    mb->set_tooltip_text("Pick how often");
    auto* pop = Gtk::make_managed<widgets::Popover>(widgets::unregistered, "drawer.repeat_pick.popover");
    auto* col = Gtk::make_managed<widgets::Box>(widgets::unregistered, "drawer.repeat_pick.column",
                                                Gtk::Orientation::VERTICAL, 0);
    col->set_margin(4);
    struct P { const char* label; const char* text; };
    const P picks[] = {
        {"Never", ""},          {"Every day", "every day"},       {"Every week", "every week"},
        {"Every 2 weeks", "every 2 weeks"},  {"Every month", "every month"},
        {"Every 3 months", "every 3 months"}, {"Every year", "every year"},
    };
    int i = 0;
    for (const auto& p : picks) {
        auto* b = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                     "drawer.repeat_pick.p" + std::to_string(i++));
        b->set_has_frame(false);
        auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                    "drawer.repeat_pick.l" + std::to_string(i));
        l->set_text(p.label);
        l->set_xalign(0.0f);
        b->set_child(*l);
        const std::string text = p.text;
        b->signal_clicked().connect([this, text, pop]() {
            m_repeat.set_text(text);
            commit_repeat();
            pop->popdown();
        });
        col->append(*b);
    }
    pop->set_child(*col);
    mb->set_popover(*pop);
    return mb;
}

// s040. The Time picker: the usual sizes. Each writes TEXT and commits it,
// so a pick and a typed value take one road (s033b's rule).
Gtk::Widget* DrawerPane::estimate_picker() {
    auto* mb = Gtk::make_managed<widgets::MenuButton>(widgets::unregistered, "drawer.est_pick");
    mb->set_icon_name("pan-down-symbolic");
    mb->set_tooltip_text("Pick how long");
    auto* pop = Gtk::make_managed<widgets::Popover>(widgets::unregistered, "drawer.est_pick.popover");
    auto* col = Gtk::make_managed<widgets::Box>(widgets::unregistered, "drawer.est_pick.column",
                                                Gtk::Orientation::VERTICAL, 0);
    col->set_margin(4);
    struct P { const char* label; const char* text; };
    const P picks[] = {
        {"No estimate", ""}, {"5 minutes", "5m"}, {"15 minutes", "15m"}, {"30 minutes", "30m"},
        {"45 minutes", "45m"}, {"1 hour", "1h"}, {"2 hours", "2h"}, {"Half a day", "4h"},
    };
    int i = 0;
    for (const auto& p : picks) {
        const std::string k = std::to_string(i++);
        auto* b = Gtk::make_managed<widgets::Button>(widgets::unregistered, "drawer.est_pick.p" + k);
        b->set_has_frame(false);
        auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered, "drawer.est_pick.l" + k);
        l->set_text(p.label);
        l->set_xalign(0.0f);
        b->set_child(*l);
        const std::string text = p.text;
        b->signal_clicked().connect([this, text, pop]() {
            m_est.set_text(text);
            commit_estimate();
            pop->popdown();
        });
        col->append(*b);
    }
    pop->set_child(*col);
    mb->set_popover(*pop);
    return mb;
}

void DrawerPane::commit_estimate() {
    if (m_loading || !m_src || m_id.empty()) return;
    const core::Node* n = m_src->find(m_id);
    if (!n) return;
    const int m = core::parse_estimate(std::string(m_est.get_text()));
    if (m < 0) {
        m_est.add_css_class("error");
        if (auto lg = log::get(log::Area::Drawer))
            lg->info("estimate '{}' on {}: refused (unparseable)", std::string(m_est.get_text()), m_id);
        return;
    }
    m_est.remove_css_class("error");
    if (m == n->task.estimate) return;
    m_src->set_estimate(m_id, m);
}

// s033. Same contract as commit_date: a rule that does not parse turns the
// field red and changes nothing.
void DrawerPane::commit_repeat() {
    if (m_loading || !m_src || m_id.empty()) return;
    const core::Node* n = m_src->find(m_id);
    if (!n) return;
    core::Repeat r = n->task.repeat;
    if (!core::repeat_parse(std::string(m_repeat.get_text()), r)) {
        m_repeat.add_css_class("error");
        if (auto lg = log::get(log::Area::Drawer))
            lg->info("repeat '{}' on {}: refused (unparseable)",
                     std::string(m_repeat.get_text()), m_id);
        return;
    }
    m_repeat.remove_css_class("error");
    if (r == n->task.repeat) return;
    m_src->set_repeat(m_id, r);
}

// Everything in the block, re-read from the node. Guarded by m_loading because
// every set_active() below emits `toggled`, and an unguarded one would write
// the value it just read straight back into the model -- marking a note
// modified for the crime of being looked at.
void DrawerPane::fill_task(const core::Node& n) {
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    m_loading = true;

    m_todo.set_active(n.task.is_task);
    m_task_body.set_visible(n.task.is_task);

    // s037b. The project half. Its dates live with the todo when it is one,
    // else here -- one set of controls either way.
    const bool project = core::is_project(*m_src, n.id);
    place_when_box(n.task.is_task);
    m_proj_check.set_active(project);
    m_proj_body.set_visible(project);
    m_proj_dates_note.set_visible(n.task.is_task);
    switch (n.task.mark) {
        case core::ProjectMark::Auto:
            m_proj_why.set_text(project
                ? "Counted automatically: it has todos directly under it, or a Children "
                  "order or Status. Untick to say it never is."
                : "Tick to make it a project — todos or not; a set of reference "
                  "notes counts too.");
            break;
        case core::ProjectMark::On:  m_proj_why.set_text(""); break;
        case core::ProjectMark::Off:
            m_proj_why.set_text("Never a project, whatever is under it. Tick to make it one.");
            break;
    }
    m_proj_why.set_visible(!m_proj_why.get_text().empty());
    if (project && !n.task.is_task) {
        m_flag.set_active(n.task.flagged);
        const std::string due = core::format_date(n.task.due);
        const std::string def = core::format_date(n.task.defer);
        if (std::string(m_due.get_text()) != due)     m_due.set_text(due);
        if (std::string(m_defer.get_text()) != def)   m_defer.set_text(def);
        m_due.remove_css_class("error");
        m_defer.remove_css_class("error");
    }

    if (n.task.is_task) {
        m_done.set_active(n.task.done);
        m_flag.set_active(n.task.flagged);
        // Only when it differs, for the same reason the Name field is: this
        // runs on every model change the drawer hears about, and a blind
        // set_text puts the cursor back at the start of a field being typed in.
        const std::string due = core::format_date(n.task.due);
        const std::string def = core::format_date(n.task.defer);
        if (std::string(m_due.get_text()) != due)     m_due.set_text(due);
        if (std::string(m_defer.get_text()) != def)   m_defer.set_text(def);
        // s040: only when the MINUTES differ, so "90" typed is not rewritten
        // to "1h 30m" under the cursor.
        if (core::parse_estimate(std::string(m_est.get_text())) != n.task.estimate)
            m_est.set_text(core::format_estimate(n.task.estimate));
        m_est.remove_css_class("error");
        m_due.remove_css_class("error");
        m_defer.remove_css_class("error");
        // s033. Only rewrite the text when the RULE differs, so "weekly"
        // typed by hand is not rewritten to "every week" under the cursor.
        {
            core::Repeat shown = n.task.repeat;
            if (!core::repeat_parse(std::string(m_repeat.get_text()), shown) ||
                shown.every != n.task.repeat.every || shown.unit != n.task.repeat.unit)
                m_repeat.set_text(core::repeat_text(n.task.repeat));
            m_repeat.remove_css_class("error");
            m_repeat_done.set_active(n.task.repeat.from_done);
            m_repeat_done.set_visible(n.task.repeat.on());
        }

        // The derived line, spelled out rather than named. "Blocked" alone
        // tells you the verdict and not the reason, and the reason is the only
        // part you can act on.
        std::string why;
        switch (core::availability(*m_src, n.id, now)) {
            case core::Avail::Available: why = "Available now."; break;
            case core::Avail::Done: {
                if (n.task.done) {
                    // s032: and WHEN, if jot was keeping time then.
                    why = n.task.finished ? "Done " + core::format_date(n.task.finished) + "."
                                          : "Done.";
                    break;
                }
                const core::NodeId by = core::stopped_by(*m_src, n.id);
                const core::Node* b = by.empty() ? nullptr : m_src->find(by);
                why = b ? "Done \u2014 \"" + titled(b) + "\" is completed."
                        : "Inside a finished todo.";
                break;
            }
            case core::Avail::Dropped:
            case core::Avail::OnHold: {
                // Name the project, because the fix is THERE, not here.
                const core::NodeId by = core::stopped_by(*m_src, n.id);
                const core::Node* b = by.empty() ? nullptr : m_src->find(by);
                const bool drop = core::availability(*m_src, n.id, now) == core::Avail::Dropped;
                if (by == n.id)  why = drop ? (n.task.finished
                                                   ? "Dropped " + core::format_date(n.task.finished) + "."
                                                   : std::string("Dropped."))
                                            : "On hold.";
                else if (b)      why = std::string(drop ? "Dropped" : "On hold") + " \u2014 \"" +
                                       titled(b) + "\" is " + (drop ? "dropped." : "on hold.");
                break;
            }
            case core::Avail::Deferred:
                why = "Deferred until " +
                      core::format_date(core::effective_defer(*m_src, n.id)) + ".";
                break;
            case core::Avail::Blocked: {
                const core::Node* p = m_src->find(n.parent_id);
                const core::NodeId next = core::next_action(*m_src, n.parent_id);
                const core::Node* nx = next.empty() ? nullptr : m_src->find(next);
                why = "Blocked \u2014 ";
                why += (p && p->task.status == core::Status::Sequential && nx)
                           ? "\"" + titled(nx) + "\" comes first."
                           : "an earlier step in a sequence is unfinished.";
                break;
            }
            case core::Avail::NotTask: break;
        }
        const std::int64_t eff = core::effective_due(*m_src, n.id);
        if (eff != 0 && eff != n.task.due)
            why += "  Due " + core::format_date(eff) + ", inherited from a parent.";
        if (n.task.repeat.on())
            why += "  Repeats " + core::repeat_text(n.task.repeat) +
                   (n.task.repeat.from_done ? " from when it is done." : ".");
        m_avail.set_text(why);
    }

    // Children ordering: offered only when there are children to order, or when
    // it is already set (so an existing choice never becomes invisible).
    const bool has_kids = !m_src->children(n.id).empty();
    m_order_row.set_visible(has_kids || n.task.status != core::Status::None);
    switch (n.task.status) {
        case core::Status::Sequential: m_order_seq.set_active(true);  break;
        case core::Status::Parallel:   m_order_par.set_active(true);  break;
        case core::Status::SingleActions: m_order_list.set_active(true); break;
        case core::Status::None:       m_order_none.set_active(true); break;
    }

    // Status: on the same terms as Children, read through project_state so a
    // ticked todo with children shows Completed.
    const core::ProjectState ps = core::project_state(n);
    m_state_row.set_visible(project);   // s037b: in the Project section, shown with it
    switch (ps) {
        case core::ProjectState::Active:    m_state_active.set_active(true); break;
        case core::ProjectState::OnHold:    m_state_hold.set_active(true);   break;
        case core::ProjectState::Completed: m_state_done.set_active(true);   break;
        case core::ProjectState::Dropped:   m_state_drop.set_active(true);   break;
    }

    // s037. Review: on the project rule, not "has children" -- a folder of
    // folders does not want a weekly look.
    m_review_row.set_visible(project);
    if (project) {
        {
            core::Repeat shown;
            if (!core::repeat_parse(std::string(m_review_every.get_text()), shown) ||
                shown.every != n.task.review.every || shown.unit != n.task.review.unit)
                m_review_every.set_text(core::repeat_text(n.task.review));
            m_review_every.remove_css_class("error");
        }
        const bool live = core::reviewable(*m_src, n.id);
        m_review_says.set_text(live ? core::review_when(n, now) + "  \u00b7  " + core::reviewed_when(n)
                                    : core::reviewed_when(n) + "  \u00b7  not reviewed while "
                                      "it, or a project it is in, is completed or dropped");
        m_review_btn.set_sensitive(live);
    }

    update_task_sensitivity(n);
    m_loading = false;
}

// Protection means read-only, and a todo's state is part of the note. Greyed
// rather than hidden: a control that vanishes looks like a missing feature,
// where a greyed one says "not here, and you know why".
void DrawerPane::update_task_sensitivity(const core::Node& n) {
    const bool on = !n.protect;
    m_todo.set_sensitive(on);
    m_done.set_sensitive(on);
    m_flag.set_sensitive(on);
    m_due.set_editable(on);
    m_defer.set_editable(on);
    m_repeat.set_editable(on);
    m_est.set_editable(on);
    m_repeat_done.set_sensitive(on);
    m_order_none.set_sensitive(on);
    m_order_seq.set_sensitive(on);
    m_order_par.set_sensitive(on);
    m_order_list.set_sensitive(on);
    for (auto* b : {&m_state_active, &m_state_hold, &m_state_done, &m_state_drop})
        b->set_sensitive(on);
    m_review_every.set_editable(on);
    m_proj_check.set_sensitive(on);   // s037b   // s037: the interval is the container's; the button is yours
}

// One section: a header button over a folding body. Everything here is
// REGISTERED -- sections are built once and live as long as the drawer, so
// `drawer.links.head` is a real address, unlike the rows rebuilt inside them.
DrawerPane::Section DrawerPane::add_section(const std::string& key) {
    install_section_css();
    const SectionSpec* sp = spec_for(key);
    Section s;
    s.key        = key;
    s.open       = sp ? sp->default_open : true;
    s.hide_empty = sp ? sp->hide_empty : false;

    s.frame = Gtk::make_managed<widgets::Box>("drawer." + key,
                                              Gtk::Orientation::VERTICAL, 2);

    s.head = Gtk::make_managed<widgets::Button>("drawer." + key + ".head");
    s.head->set_has_frame(false);
    s.head->add_css_class("drawer-section-head");
    auto* hbox = Gtk::make_managed<widgets::Box>("drawer." + key + ".head_box",
                                                 Gtk::Orientation::HORIZONTAL, 6);
    s.arrow = Gtk::make_managed<widgets::Image>("drawer." + key + ".arrow");
    s.title = Gtk::make_managed<widgets::Label>("drawer." + key + ".title");
    s.title->set_text(sp ? sp->heading : key);
    s.title->set_xalign(0.0f);
    s.title->set_hexpand(true);
    s.title->add_css_class("caption-heading");
    s.count = Gtk::make_managed<widgets::Label>("drawer." + key + ".count");
    s.count->add_css_class("dim-label");
    s.count->add_css_class("caption");
    hbox->append(*s.arrow);
    hbox->append(*s.title);
    hbox->append(*s.count);
    s.head->set_child(*hbox);
    s.frame->append(*s.head);

    s.body = Gtk::make_managed<widgets::Box>("drawer." + key + ".body",
                                             Gtk::Orientation::VERTICAL, 4);
    s.body->set_margin_start(22);   // under the heading's text, not its arrow
    s.body->set_margin_bottom(6);
    s.rows = Gtk::make_managed<widgets::Box>("drawer." + key + ".rows",
                                             Gtk::Orientation::VERTICAL, 2);
    s.body->append(*s.rows);
    s.frame->append(*s.body);
    m_column.append(*s.frame);

    // The click finds its section by KEY, not by a captured pointer or `this`
    // plus an index: Section is a value copied into a member after this
    // returns, so the address it has now is not the one it will live at.
    s.head->signal_clicked().connect([this, key]() {
        for (Section* sec : m_all) {
            if (sec->key != key) continue;
            sec->open = !sec->open;
            apply_open(*sec);
            if (auto lg = log::get(log::Area::Drawer))
                lg->debug("section {} {}", key, sec->open ? "opened" : "closed");
            m_sig_section.emit(key, sec->open);
            return;
        }
    });

    apply_open(s);
    return s;
}

void DrawerPane::apply_open(Section& s) {
    if (!s.body || !s.arrow) return;
    s.body->set_visible(s.open);
    s.arrow->set_from_icon_name(s.open ? "pan-down-symbolic" : "pan-end-symbolic");
    s.head->set_tooltip_text(s.open ? "Fold this section" : "Open this section");
}

void DrawerPane::set_count(Section& s, std::size_t n) {
    if (s.count) s.count->set_text(n ? std::to_string(n) : std::string{});
}

void DrawerPane::set_section_states(const std::map<std::string, bool>& open) {
    for (Section* s : m_all) {
        auto it = open.find(s->key);
        const SectionSpec* sp = spec_for(s->key);
        s->open = (it != open.end()) ? it->second : (sp ? sp->default_open : true);
        apply_open(*s);
    }
}

// The rebuilt part only. A hide-when-empty section goes; the others stay on
// screen and their fill_ puts the rows back.
void DrawerPane::clear(Section& s) {
    if (!s.rows) return;
    while (auto* c = s.rows->get_first_child()) s.rows->remove(*c);
    set_count(s, 0);
    if (s.frame && s.hide_empty) s.frame->set_visible(false);
}

void DrawerPane::show_sections(bool on) {
    for (Section* s : m_all)
        if (s->frame) s->frame->set_visible(on && !s->hide_empty);
}

// A row you can click to go somewhere. Flat, left-aligned, and UNREGISTERED:
// these are rebuilt on every selection change, and a live address book full of
// hundreds of rebuilt rows is a dumping ground rather than a diagnosis tool.
Gtk::Widget* DrawerPane::link_row(const std::string& label, const std::string& detail,
                                  const core::NodeId& go_to, bool dangling) {
    if (go_to.empty() || dangling) {
        // Not clickable, and that is deliberate: a row that looks like a button
        // and does nothing when pressed is worse than a row that never offered.
        auto* box = Gtk::make_managed<widgets::Box>(
            widgets::unregistered, "drawer.row", Gtk::Orientation::VERTICAL, 0);
        auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered, "drawer.row_label");
        l->set_text(label);
        l->set_xalign(0.0f);
        l->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        if (dangling) l->add_css_class("dim-label");
        box->append(*l);
        if (!detail.empty()) {
            auto* d = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                        "drawer.row_detail");
            d->set_text(detail);
            d->set_xalign(0.0f);
            d->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
            d->add_css_class("dim-label");
            d->add_css_class("caption");
            box->append(*d);
        }
        return box;
    }

    auto* b = Gtk::make_managed<widgets::Button>(widgets::unregistered, "drawer.row_button");
    b->set_has_frame(false);
    auto* box = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, "drawer.row", Gtk::Orientation::VERTICAL, 0);
    auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered, "drawer.row_label");
    l->set_text(label);
    l->set_xalign(0.0f);
    l->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    box->append(*l);
    if (!detail.empty()) {
        auto* d = Gtk::make_managed<widgets::Label>(widgets::unregistered, "drawer.row_detail");
        d->set_text(detail);
        d->set_xalign(0.0f);
        d->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        d->add_css_class("dim-label");
        d->add_css_class("caption");
        box->append(*d);
    }
    b->set_child(*box);
    b->signal_clicked().connect([this, go_to]() { m_sig_goto.emit(go_to); });
    return b;
}

Gtk::Widget* DrawerPane::fact_row(const std::string& text, bool dim) {
    auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered, "drawer.fact");
    l->set_text(text);
    l->set_xalign(0.0f);
    l->set_wrap(true);
    if (dim) l->add_css_class("dim-label");
    return l;
}

void DrawerPane::set_source(core::NodeSource* src, const core::LinkIndex* index) {
    m_src   = src;
    m_index = index;
    show_node("");
}

void DrawerPane::set_jots_dir(const std::string& dir) {
    m_jots_dir = dir;
    if (!m_id.empty()) refresh();
}

void DrawerPane::set_attach(const core::AttachStore* store) {
    m_attach = store;
    if (!m_id.empty()) refresh();
}

void DrawerPane::refresh() {
    const core::NodeId id = m_id;
    show_node(id);
}

void DrawerPane::show_node(const core::NodeId& id) {
    // Held while a row menu is open (see m_menus_open). Only a refresh of the
    // SAME note is held -- a different note can only be asked for by a click
    // elsewhere, which closes the menu first.
    if (m_menus_open > 0 && id == m_id) {
        m_refresh_pending = true;
        if (auto lg = log::get(log::Area::Drawer))
            lg->debug("refresh held: a row menu is open");
        return;
    }
    m_id = id;
    // The rows (and any menu they own) are about to go. A count that outlived
    // its button would hold every refresh after it, forever.
    m_menus_open = 0;
    m_refresh_pending = false;

    for (Section* sec : m_all) clear(*sec);

    const core::Node* n = (m_src && !id.empty()) ? m_src->find(id) : nullptr;
    if (!n) {
        m_id.clear();
        m_empty.set_visible(true);
        m_copy_link.set_visible(false);
        m_name_head.set_visible(false);
        m_name.set_visible(false);
        show_sections(false);
        return;
    }

    m_empty.set_visible(false);
    m_name_head.set_visible(true);
    m_name.set_visible(true);
    // The always-present sections come back; hide-when-empty ones are left to
    // their fill_ to reveal, so an empty one never flashes on screen.
    show_sections(true);

    // Only when it differs. show_node() runs on every model change the drawer
    // cares about, and an unconditional set_text would put the cursor back at
    // the start of the field on every keystroke typed into it.
    m_loading = true;
    if (std::string(m_name.get_text()) != n->title) m_name.set_text(n->title);
    m_name.set_editable(!n->protect);
    m_loading = false;
    m_copy_link.set_visible(true);

    fill_task(*n);
    fill_links(*n);
    fill_backlinks(*n);
    fill_tags(*n);
    fill_enclosures(*n);
    fill_structure(*n);
    fill_file(*n);
    fill_identity(*n);

    // A rows box with nothing in it still takes a spacing slot in its body, so
    // a section whose content is all HELD widgets (Todo, Identity) would carry
    // a gap above them. Hidden when empty, it costs nothing.
    for (Section* sec : m_all)
        if (sec->rows) sec->rows->set_visible(sec->rows->get_first_child() != nullptr);

    // The trace channel's half. The drawer's whole job is to report what is
    // true of a note, so "what did it think was true" is the first question
    // when it shows something surprising -- and I cannot see the screen, so
    // this is the only channel I have on it at all.
    if (auto lg = log::get(log::Area::Drawer)) {
        const core::Scan sc = core::scan(n->body);
        lg->debug("note={} links={} backlinks={} tags={} protected={}", n->id,
                  sc.links.size(),
                  m_index ? m_index->incoming(n->id).size() : 0u, sc.tags.size(),
                  n->protect);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Links out. The scan already knows; this resolves ids to titles.
//
// Three kinds of row, and the difference between them is the honest part:
//   * a jot: link to a note that exists     -> clickable, shows the CURRENT title
//   * a jot: link to a note that does not   -> dimmed, says so, not clickable
//   * anything else (http, a web image)       -> the target as written, not clickable
//   (an attachment is not here: s016b gave it the Enclosures section)
//
// Showing the note's current title rather than the link's label is deliberate:
// a link written as "the grocery thing" pointing at a note since renamed to
// "Shopping" should say Shopping, because you are about to click it and go
// there. The label is kept as the second line so the note's own words survive.
// ─────────────────────────────────────────────────────────────────────────────
void DrawerPane::fill_links(const core::Node& n) {
    const core::Scan sc = core::scan(n.body);
    if (sc.links.empty()) return;

    std::size_t shown = 0;
    for (const auto& lk : sc.links) {
        // An attachment is an ENCLOSURE (s016b) and has its own section with a
        // thumbnail and a status. Listing it here too would be the same fact
        // twice, in the less useful of the two places.
        if (!core::attachment_name(lk.target).empty()) continue;
        if (!core::linked_key(lk.target).empty()) continue;   // s019: a linked file is one too
        ++shown;
        const std::string label = lk.label.empty() ? std::string("(no label)") : lk.label;
        const core::NodeId to = core::link_node_id(lk.target);

        if (to.empty()) {
            const std::string kind = lk.image ? "image" : "external";
            m_links.rows->append(*link_row(label, lk.target + "  \u00b7  " + kind, "", false));
            continue;
        }
        const core::Node* target = m_src ? m_src->find(to) : nullptr;
        if (!target) {
            m_links.rows->append(
                *link_row(label, "this note no longer exists", "", /*dangling=*/true));
            continue;
        }
        const std::string now = titled(target);
        m_links.rows->append(
            *link_row(now, now == label ? std::string{} : "linked as \u201c" + label + "\u201d",
                      to, false));
    }
    if (shown == 0) return;
    set_count(m_links, shown);
    m_links.frame->set_visible(true);
}

// Backlinks. The half that needed an index, and the half that makes a link a
// relationship rather than a jump.
void DrawerPane::fill_backlinks(const core::Node& n) {
    if (!m_index) return;
    const auto& in = m_index->incoming(n.id);
    if (in.empty()) return;

    // One note may mention this one several times. The index keeps every
    // mention (the count has to agree with the text), but a LIST of identical
    // rows is noise -- so collapse per source and say how many.
    std::vector<core::NodeId> seen;
    for (const auto& e : in) {
        if (std::find(seen.begin(), seen.end(), e.from) != seen.end()) continue;
        seen.push_back(e.from);

        const core::Node* from = m_src ? m_src->find(e.from) : nullptr;
        if (!from) continue;                 // a source that has since gone
        const int mentions = static_cast<int>(
            std::count_if(in.begin(), in.end(),
                          [&](const core::LinkRef& r) { return r.from == e.from; }));
        std::string detail;
        if (mentions > 1) detail = std::to_string(mentions) + " mentions";
        m_backlinks.rows->append(*link_row(titled(from), detail, e.from, false));
    }
    set_count(m_backlinks, seen.size());   // notes, not mentions -- matches the rows
    if (m_backlinks.rows->get_first_child()) m_backlinks.frame->set_visible(true);
}

// Tags (s035b). Each tag the note carries, as a chip. A tag on the TAG LINE
// (the note's last line, only tags) has an x -- Note details owns that line.
// A tag written anywhere else is marked "in text": you remove it where you
// wrote it. Clicking a chip's name opens the Tags view on it (s035).
void DrawerPane::fill_tags(const core::Node& n) {
    const core::Scan sc = core::scan(n.body);
    const core::TagLine tl = core::find_tag_line(n.body);

    std::vector<std::string> keys, names;
    std::vector<bool> on_line;
    for (const auto& t : sc.tags) {
        const std::string k = core::tag_key(t.name);
        if (k.empty() || std::find(keys.begin(), keys.end(), k) != keys.end()) continue;
        keys.push_back(k);
        names.push_back(t.name);
        bool line = false;
        if (tl.found)
            for (const auto& ln : tl.names) line = line || core::tag_key(ln) == k;
        on_line.push_back(line);
    }
    if (m_tag_entry) m_tag_entry->set_sensitive(!n.protect);
    set_count(m_tags, names.size());
    m_tags.frame->set_visible(true);
    if (names.empty()) return;

    auto* flow = Gtk::make_managed<widgets::FlowBox>(widgets::unregistered, "drawer.tags.flow");
    flow->set_selection_mode(Gtk::SelectionMode::NONE);
    flow->set_max_children_per_line(20);
    flow->set_column_spacing(2);
    flow->set_row_spacing(2);
    flow->set_halign(Gtk::Align::START);
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto* chip = Gtk::make_managed<widgets::Box>(widgets::unregistered,
                                                     "drawer.tagchip." + keys[i],
                                                     Gtk::Orientation::HORIZONTAL, 0);
        chip->set_halign(Gtk::Align::START);
        auto* b = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                     "drawer.tag." + keys[i]);
        b->set_label("#" + names[i]);
        b->set_has_frame(false);
        b->add_css_class("jot-drawer-tag");
        const std::string name = names[i];
        b->signal_clicked().connect([this, name]() { m_sig_tag.emit(name); });
        chip->append(*b);
        if (on_line[i] && !n.protect) {
            b->set_tooltip_text("Show everything tagged #" + name);
            auto* x = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                         "drawer.tag_remove." + keys[i]);
            x->set_label("×");
            x->set_has_frame(false);
            x->add_css_class("jot-drawer-tag-x");
            x->set_tooltip_text("Take #" + name + " off this note");
            x->signal_clicked().connect([this, name]() { m_sig_tag_remove.emit(name); });
            chip->append(*x);
        } else {
            b->set_tooltip_text(on_line[i] ? "Show everything tagged #" + name
                                           : "Written in the text -- remove it there.\n"
                                             "Click to show everything tagged #" + name);
            if (!on_line[i]) {
                auto* where = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                                "drawer.tag_intext." + keys[i]);
                where->set_text("in text");
                where->add_css_class("dim-label");
                where->add_css_class("caption");
                chip->append(*where);
            }
        }
        flow->append(*chip);
    }
    m_tags.rows->append(*flow);
}

// s035b. The field under the chips: type a tag and press Enter, or pick one
// from the list of every tag in the folder (the ▾). The same shape as the
// Repeat field (s033b), entry and list joined.
void DrawerPane::build_tag_field() {
    m_tag_add_row = Gtk::make_managed<widgets::Box>("drawer.tag_add_row",
                                                    Gtk::Orientation::HORIZONTAL, 0);
    m_tag_add_row->add_css_class("linked");
    m_tag_add_row->set_margin_top(4);
    m_tag_entry = Gtk::make_managed<widgets::Entry>("drawer.tag_entry");
    m_tag_entry->set_placeholder_text("Add a tag…");
    m_tag_entry->set_tooltip_text("Type a tag and press Enter, or pick one from the list. "
                                  "It goes on the note's tag line, at the bottom.");
    m_tag_entry->set_hexpand(true);
    m_tag_entry->signal_activate().connect([this]() { commit_tag_entry(); });
    m_tag_entry->signal_changed().connect([this]() { m_tag_entry->remove_css_class("error"); });
    m_tag_add_row->append(*m_tag_entry);

    auto* mb = Gtk::make_managed<widgets::MenuButton>("drawer.tag_pick");
    mb->set_icon_name("pan-down-symbolic");
    mb->set_tooltip_text("Pick one of your tags");
    m_tag_pick_pop = Gtk::make_managed<widgets::Popover>("drawer.tag_pick.popover");
    auto* scroll = Gtk::make_managed<widgets::ScrolledWindow>("drawer.tag_pick.scroll");
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_propagate_natural_height(true);
    scroll->set_max_content_height(320);
    m_tag_pick_col = Gtk::make_managed<widgets::Box>("drawer.tag_pick.column",
                                                     Gtk::Orientation::VERTICAL, 0);
    m_tag_pick_col->set_margin(4);
    scroll->set_child(*m_tag_pick_col);
    m_tag_pick_pop->set_child(*scroll);
    // Filled each time it opens: the folder's tags change as you type notes,
    // and what is in the field narrows the list.
    m_tag_pick_pop->signal_show().connect([this]() { fill_tag_picks(); });
    mb->set_popover(*m_tag_pick_pop);
    m_tag_add_row->append(*mb);

    m_tags.body->append(*m_tag_add_row);
}

void DrawerPane::fill_tag_picks() {
    while (auto* c = m_tag_pick_col->get_first_child()) m_tag_pick_col->remove(*c);
    if (!m_src) return;
    const core::Node* n = m_src->find(m_id);
    std::vector<std::string> mine;
    if (n) mine = core::node_tag_keys(*n);
    std::string want = core::tag_key(core::clean_tag_name(std::string(m_tag_entry->get_text())));

    int shown = 0;
    for (const auto& t : core::tag_list(*m_src, static_cast<std::int64_t>(std::time(nullptr)))) {
        if (std::find(mine.begin(), mine.end(), t.key) != mine.end()) continue;
        // s035b: only tags someone WROTE. #home/garden is one entry, not two --
        // its implied parent #home is a way to browse (the Tags tab), not a
        // tag to hand out.
        if (!t.written) continue;
        if (!want.empty() && t.key.find(want) == std::string::npos) continue;
        auto* b = Gtk::make_managed<widgets::Button>(widgets::unregistered,
                                                     "drawer.tag_pick." + t.key);
        b->set_has_frame(false);
        auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered,
                                                    "drawer.tag_pick.l." + t.key);
        l->set_text("#" + t.name);
        l->set_xalign(0.0f);
        b->set_child(*l);
        const std::string name = t.name;
        b->signal_clicked().connect([this, name]() {
            m_tag_pick_pop->popdown();
            m_tag_entry->set_text("");
            m_sig_tag_add.emit(name);
        });
        m_tag_pick_col->append(*b);
        ++shown;
    }
    if (shown == 0) {
        auto* l = Gtk::make_managed<widgets::Label>(widgets::unregistered, "drawer.tag_pick.none");
        l->set_text(want.empty() ? "No other tags yet.\nType one and press Enter."
                                 : "No tag like that yet.\nPress Enter to make it.");
        l->add_css_class("dim-label");
        l->set_margin(8);
        m_tag_pick_col->append(*l);
    }
}

void DrawerPane::commit_tag_entry() {
    if (m_loading || !m_src || m_id.empty()) return;
    const std::string typed = m_tag_entry->get_text();
    if (typed.find_first_not_of(" \t#") == std::string::npos) { m_tag_entry->set_text(""); return; }
    const std::string name = core::clean_tag_name(typed);
    if (name.empty()) {
        // Same contract as the date fields: red, nothing written.
        m_tag_entry->add_css_class("error");
        m_tag_entry->set_tooltip_text("A tag starts with a letter; then letters, digits, - _ or /");
        return;
    }
    m_tag_entry->set_text("");
    m_sig_tag_add.emit(name);
}

// Where the note sits. This is what the s005 status line carried, and the
// status line retires now that this exists -- not before, or the milestone
// would have lost information to make a point.
//
// The PARENT'S TITLE, never its id. An id in this row would be the one piece of
// hex the drawer exists to fold away, sitting in the least foldable place.
void DrawerPane::fill_structure(const core::Node& n) {
    const bool root = n.parent_id.empty();
    if (root) {
        m_structure.rows->append(*fact_row("Top level", false));
    } else {
        const core::Node* p = m_src ? m_src->find(n.parent_id) : nullptr;
        m_structure.rows->append(
            *fact_row("Under " + (p ? titled(p) : std::string("a note that is missing")),
                      false));
    }

    const std::size_t kids = m_src ? m_src->children(n.id).size() : 0;
    m_structure.rows->append(*fact_row(
        std::to_string(kids) + (kids == 1 ? " child" : " children"), true));

    if (n.protect)
        m_structure.rows->append(*fact_row("Protected \u2014 read-only", false));

    m_structure.frame->set_visible(true);
}

// The NOTE's file. The JOTS FOLDER is app-level and lives in the header, where
// s005 put it; two panels at two altitudes and neither repeats the other.
void DrawerPane::fill_file(const core::Node& n) {
    if (m_jots_dir.empty()) {
        m_file.rows->append(*fact_row("Not saved to disk yet", true));
        m_file.frame->set_visible(true);
        return;
    }

    const std::filesystem::path path =
        std::filesystem::path(m_jots_dir) / "notes" / (n.id + ".md");
    m_file.rows->append(*fact_row("notes/" + n.id + ".md", false));

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        const auto size = std::filesystem::file_size(path, ec);
        std::string line = ec ? std::string{} : human_size(size);
        // The MODEL's timestamp, not the file's. A body edit is dirty for up to
        // two seconds before the flush timer lands it, and reading the file's
        // mtime would show the drawer disagreeing with the note in front of you
        // for exactly as long as the deferred write takes.
        const std::string mod = when(n.modified);
        if (!mod.empty()) line += (line.empty() ? "" : "  \u00b7  ") + mod;
        if (!line.empty()) m_file.rows->append(*fact_row(line, true));
    } else {
        // Structure writes immediately; bodies land on the flush. A note created
        // seconds ago legitimately has no file yet.
        m_file.rows->append(*fact_row("not written yet", true));
    }
    m_file.frame->set_visible(true);
}

// The drawer's own write. The model is the only channel between the three
// rename surfaces -- this never reaches into the tree or the editor, and they
// never reach in here.
void DrawerPane::write_title() {
    if (m_loading || !m_src || m_id.empty()) return;
    m_src->set_title(m_id, std::string(m_name.get_text()));
}

void DrawerPane::title_changed_elsewhere(const std::string& title) {
    // If this entry has focus, the change came FROM here and has already been
    // applied; setting the text back would fight the cursor.
    if (m_name.has_focus()) return;
    if (std::string(m_name.get_text()) == title) return;
    m_loading = true;
    m_name.set_text(title);
    m_loading = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Enclosures (s016b). The list is core::enclosures(): every attachment the body
// references, once per file, joined with what the store remembers. This file
// only draws it.
//
// A row: thumbnail (an image) or type icon (any other file, s018) on the left,
// name over a detail line on the right. The
// detail says what a Usage panel says -- size, where it came from, when -- and
// "Missing" in place of all of it when the note points at a file that is not
// in the folder, because that is the one status that needs acting on.
// ─────────────────────────────────────────────────────────────────────────────
void DrawerPane::fill_enclosures(const core::Node& n) {
    static const core::AttachStore kNone{};
    const auto list = core::enclosures(n.body, m_attach ? *m_attach : kNone);
    if (list.empty()) return;
    for (const auto& e : list) m_enclosures.rows->append(*enclosure_row(e));
    set_count(m_enclosures, list.size());
    m_enclosures.frame->set_visible(true);

    if (auto lg = log::get(log::Area::Drawer)) {
        std::size_t missing = 0;
        for (const auto& e : list) missing += e.present ? 0 : 1;
        lg->debug("enclosures: {} file(s), {} missing, store '{}'", list.size(), missing,
                  m_attach ? m_attach->dir : std::string("(none)"));
    }
}

Gtk::Widget* DrawerPane::enclosure_row(const core::Enclosure& e) {
    constexpr int kThumb = 56;
    auto* row = Gtk::make_managed<widgets::Box>(widgets::unregistered, "drawer.enclosure",
                                                Gtk::Orientation::HORIZONTAL, 8);
    row->set_margin_bottom(4);

    // Where the bytes are: attachments/<name>, or wherever a linked file lives.
    const std::string path = e.path;
    const std::string shown = e.display();
    const bool modified = e.status == core::EnclosureStatus::Modified;
    // s020: an embed that knows its original can be linked to it instead --
    // which also rescues a Missing embed whose original is still out there.
    const std::string original = (!e.linked && m_attach) ? core::original_of(*m_attach, e.name)
                                                          : std::string{};

    // The thumbnail, or a stand-in, in the SAME square either way so every
    // name starts on one edge. A GtkImage at a pixel size fits a paintable
    // into that square keeping its aspect (a GtkPicture sizes itself to the
    // picture, which is what made the first look ragged) and draws from the
    // 2x-decoded texture on a HiDPI screen.
    //
    // s018: only an IMAGE is decoded. Any other file shows its type's icon,
    // looked up from the name (the same guess Files makes), so a PDF looks
    // like a PDF and nothing is ever read to draw the row.
    const bool picture = core::is_image_filename(shown);
    bool uncertain = false;
    const std::string ctype = Gio::content_type_guess(shown, nullptr, 0, uncertain);
    Glib::RefPtr<Gdk::Texture> tex =
        (e.present && picture) ? thumbnail(path) : Glib::RefPtr<Gdk::Texture>{};
    auto* img = Gtk::make_managed<widgets::Image>(widgets::unregistered, "drawer.enclosure_thumb");
    if (tex) {
        img->set(tex);
    } else if (picture) {
        img->set_from_icon_name(e.present ? "image-x-generic-symbolic" : "image-missing");
        img->add_css_class("dim-label");
    } else {
        if (auto icon = Gio::content_type_get_icon(ctype)) img->set(icon);
        else img->set_from_icon_name("text-x-generic");
        if (!e.present) img->add_css_class("dim-label");
    }
    img->set_pixel_size(kThumb);
    img->set_size_request(kThumb, kThumb);
    img->set_valign(Gtk::Align::START);
    row->append(*img);

    auto* text = Gtk::make_managed<widgets::Box>(widgets::unregistered, "drawer.enclosure_text",
                                                 Gtk::Orientation::VERTICAL, 0);
    text->set_hexpand(true);
    text->set_valign(Gtk::Align::CENTER);

    auto* name = Gtk::make_managed<widgets::Label>(widgets::unregistered, "drawer.row_label");
    name->set_text(shown);
    name->set_xalign(0.0f);
    name->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    text->append(*name);

    std::string detail;
    if (!e.present) {
        detail = "Missing";
    } else {
        std::error_code ec;
        const auto sz = std::filesystem::file_size(path, ec);
        detail = ec ? std::string{} : human_size(sz);
        // What it is, for a file whose icon may be the generic page: "PDF
        // document", "Zip archive". An image says it with its thumbnail.
        if (!picture && ctype != "application/octet-stream") {
            const std::string kind = Gio::content_type_get_description(ctype);
            if (!kind.empty()) detail = kind + (detail.empty() ? "" : "  \u00b7  " + detail);
        }
        std::string from;
        if (e.linked)
            from = "linked";          // s019: says where the file lives, not how it came
        else if (e.has_meta)
            from = e.meta.source == "clipboard" ? std::string("pasted") : std::string("from Files");
        if (!from.empty()) detail += (detail.empty() ? "" : "  \u00b7  ") + from;
        // Modified leads: it is the one thing on the line that asks something.
        if (modified) detail = "Modified  \u00b7  " + detail;
        if (e.refs > 1) detail += "  \u00b7  " + std::to_string(e.refs) + " references";
    }
    auto* d = Gtk::make_managed<widgets::Label>(widgets::unregistered, "drawer.row_detail");
    d->set_text(detail);
    d->set_xalign(0.0f);
    d->set_ellipsize(Pango::EllipsizeMode::END);
    // Missing is the one status that asks for something to be done, so it is
    // the one that is not dimmed.
    d->add_css_class(!e.present ? "error" : modified ? "drawer-modified" : "dim-label");
    d->add_css_class("caption");
    text->append(*d);
    row->append(*text);

    // The long form, on hover: where it came from and when. A path is too long
    // for a detail line and exactly what a tooltip is for.
    std::string tip = e.linked ? path : std::string(core::kAttachPrefix) + e.name;
    if (!e.present)
        tip += e.linked ? "\nThe note points at this file, but it is not there. Relink it from the menu."
                        : "\nThe note points at this file, but it is not in the attachments folder.";
    if (modified) tip += "\nChanged since it was linked. Accept Change to call it current.";
    if (e.linked) {
        if (e.has_meta)
            if (const std::string at = when(e.meta.added); !at.empty()) tip += "\nLinked " + at;
        tip += "\nLinked \u2014 jot points at this file where it lives; nothing is copied";
    } else if (e.has_meta) {
        if (!e.meta.source.empty())
            tip += "\n" + (e.meta.source == "clipboard" ? std::string("Pasted from the clipboard")
                                                         : "Copied from " + e.meta.source);
        if (const std::string at = when(e.meta.added); !at.empty()) tip += "\nAdded " + at;
        tip += "\nEmbedded \u2014 jot keeps its own copy";
        if (!original.empty()) tip += ". Link Instead points the note at the original.";
    }
    row->set_tooltip_text(tip);

    // The actions (s017). Only for a file that is THERE -- a Missing row has
    // nothing to open or copy, and a menu of greyed items would be a menu
    // that exists to say no. Double-clicking the thumbnail is Open: the one
    // verb you reach for without reading.
    // s019: a LINKED row always has a menu -- a Missing link is the one row
    // with the most to do (Relink), which is not true of a missing embed.
    if (e.present || e.linked || !original.empty()) {
        const auto target = Glib::Variant<Glib::ustring>::create(e.name);
        auto menu = Gio::Menu::create();
        auto add = [&](const char* label, const char* action) {
            auto item = Gio::MenuItem::create(label, "");
            item->set_action_and_target(action, target);
            menu->append_item(item);
        };
        if (e.present) {
            add("Open", "encl.open");
            add("Show in Files", "encl.reveal");
            auto out = Gio::Menu::create();
            auto add_out = [&](const char* label, const char* action) {
                auto item = Gio::MenuItem::create(label, "");
                item->set_action_and_target(action, target);
                out->append_item(item);
            };
            if (picture) add_out("Copy Image", "encl.copy");   // a picture only (s018)
            add_out("Save a Copy\u2026", "encl.save");
            menu->append_section(out);
        }
        if (e.linked) {
            auto lk = Gio::Menu::create();
            auto add_lk = [&](const char* label, const char* action) {
                auto item = Gio::MenuItem::create(label, "");
                item->set_action_and_target(action, target);
                lk->append_item(item);
            };
            if (modified) add_lk("Accept Change", "encl.accept");
            add_lk("Relink\u2026", "encl.relink");
            if (e.present) add_lk("Embed a Copy", "encl.embed");   // s020
            menu->append_section(lk);
        } else if (!original.empty()) {
            // s020: the other direction. Its own section, like the linked
            // verbs: it changes how the note carries the file, not the file.
            auto cv = Gio::Menu::create();
            auto item = Gio::MenuItem::create("Link Instead", "");
            item->set_action_and_target("encl.link", target);
            cv->append_item(item);
            menu->append_section(cv);
        }

        auto* more = Gtk::make_managed<Gtk::MenuButton>();
        more->set_icon_name("view-more-symbolic");
        more->set_has_frame(false);
        more->set_valign(Gtk::Align::CENTER);
        more->set_tooltip_text(!e.present ? (e.linked ? "Relink this file"
                                                      : "Link this file to its original")
                               : picture  ? "Open, show in Files, copy or save this image"
                                          : "Open, show in Files, or save a copy of this file");
        more->set_menu_model(menu);
        row->append(*more);

        // Count the menu open and closed. On close, a held refresh runs on an
        // IDLE, never inside the hide -- the hide is itself an event on the
        // popover the refresh would destroy.
        if (auto* pop = more->get_popover()) {
            pop->signal_show().connect([this]() { ++m_menus_open; });
            pop->signal_hide().connect([this]() {
                if (m_menus_open > 0) --m_menus_open;
                if (m_menus_open == 0 && m_refresh_pending) {
                    m_refresh_pending = false;
                    Glib::signal_idle().connect_once([this]() { refresh(); });
                }
            });
        }

        if (e.present) {
            auto dbl = Gtk::GestureClick::create();
            dbl->set_button(GDK_BUTTON_PRIMARY);
            const std::string name = e.name;
            dbl->signal_pressed().connect([this, name](int n, double, double) {
                if (n == 2) m_sig_enclosure.emit("open", name);
            });
            img->add_controller(dbl);
        }
    }
    return row;
}

Glib::RefPtr<Gdk::Texture> DrawerPane::thumbnail(const std::string& path) {
    std::error_code ec;
    const auto size = static_cast<std::int64_t>(std::filesystem::file_size(path, ec));
    if (ec) return {};
    const auto mt = std::filesystem::last_write_time(path, ec);
    const std::int64_t stamp = ec ? 0 : static_cast<std::int64_t>(mt.time_since_epoch().count());

    if (auto it = m_thumbs.find(path);
        it != m_thumbs.end() && it->second.stamp == stamp && it->second.size == size)
        return it->second.tex;

    // Decoded at thumbnail size by the pixbuf loader (which also knows SVG),
    // then handed to GTK as PNG bytes -- the non-deprecated road from a
    // pixbuf to a texture. A file the loader cannot read gets the stand-in
    // icon, and is remembered as such so it is not retried every keystroke.
    Glib::RefPtr<Gdk::Texture> tex;
    try {
        auto pb = Gdk::Pixbuf::create_from_file(path, 112, 112, true);
        gchar* buf = nullptr;
        gsize len = 0;
        pb->save_to_buffer(buf, len, "png");
        auto bytes = Glib::Bytes::create(buf, len);
        g_free(buf);
        tex = Gdk::Texture::create_from_bytes(bytes);
    } catch (const Glib::Error& e) {
        if (auto lg = log::get(log::Area::Drawer))
            lg->warn("thumbnail '{}': {}", path, e.what());
    }
    m_thumbs[path] = Thumb{stamp, size, tex};
    return tex;
}

void DrawerPane::fill_identity(const core::Node& n) {
    m_uuid.set_text(n.id);
}

}  // namespace jot
