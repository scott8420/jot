#include "DrawerPane.hpp"
#include "Log.hpp"
#include "core/Markdown.hpp"

#include <gtkmm/cssprovider.h>
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
    {"todo",      "Todo",        true,  false},
    {"structure", "Structure",   true,  false},
    {"links",     "Links",       true,  true },
    {"backlinks", "Linked from", true,  true },
    {"tags",      "Tags",        true,  true },
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
        ".drawer-section-head { padding: 3px 4px 3px 0; min-height: 0; }");
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
      m_done("drawer.done"),
      m_flag("drawer.flag"),
      m_due_row("drawer.due_row", Gtk::Orientation::HORIZONTAL, 8),
      m_due_label("drawer.due_label"),
      m_due("drawer.due"),
      m_defer_row("drawer.defer_row", Gtk::Orientation::HORIZONTAL, 8),
      m_defer_label("drawer.defer_label"),
      m_defer("drawer.defer"),
      m_avail("drawer.avail"),
      m_order_row("drawer.order_row", Gtk::Orientation::VERTICAL, 2),
      m_order_label("drawer.order_label"),
      m_order_none("drawer.order_none"),
      m_order_seq("drawer.order_seq"),
      m_order_par("drawer.order_par"),
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
    m_todo_sec  = add_section("todo");
    m_structure = add_section("structure");
    m_links     = add_section("links");
    m_backlinks = add_section("backlinks");
    m_tags      = add_section("tags");
    m_file      = add_section("file");
    m_identity  = add_section("identity");
    m_all = {&m_todo_sec, &m_structure, &m_links, &m_backlinks,
             &m_tags,     &m_file,      &m_identity};

    build_task_block();

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
    m_task_body.append(m_flag);

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
        row.append(entry);
        m_task_body.append(row);
    };
    date_field(m_due_row, m_due_label, m_due, "Due",
               "YYYY-MM-DD, YYYY-MM-DD HH:MM, today, tomorrow. "
               "A bare date means the end of that day.", core::DateKind::Due);
    date_field(m_defer_row, m_defer_label, m_defer, "Defer",
               "Do not surface it before this. A bare date means the start of "
               "that day.", core::DateKind::Defer);

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
    // Radio rather than a dropdown: three options, all of which want to be
    // readable without opening anything, and the middle one is the one people
    // never discover.
    m_order_label.set_text("Children");
    m_order_label.set_xalign(0.0f);
    m_order_label.add_css_class("dim-label");
    m_order_label.add_css_class("caption-heading");
    m_order_row.append(m_order_label);

    m_order_none.set_label("Unordered");
    m_order_seq.set_label("Sequential \u2014 one at a time, in order");
    m_order_par.set_label("Parallel \u2014 all available at once");
    m_order_seq.set_group(m_order_none);
    m_order_par.set_group(m_order_none);
    auto order_write = [this](core::Status st) {
        return [this, st]() {
            if (m_loading || !m_src || m_id.empty()) return;
            m_src->set_status(m_id, st);
        };
    };
    m_order_none.signal_toggled().connect([this, order_write]() {
        if (m_order_none.get_active()) order_write(core::Status::None)();
    });
    m_order_seq.signal_toggled().connect([this, order_write]() {
        if (m_order_seq.get_active()) order_write(core::Status::Sequential)();
    });
    m_order_par.signal_toggled().connect([this, order_write]() {
        if (m_order_par.get_active()) order_write(core::Status::Parallel)();
    });
    m_order_row.append(m_order_none);
    m_order_row.append(m_order_seq);
    m_order_row.append(m_order_par);
    m_order_row.set_margin_top(6);
    m_structure.body->append(m_order_row);   // after the rebuilt rows, held
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

// Everything in the block, re-read from the node. Guarded by m_loading because
// every set_active() below emits `toggled`, and an unguarded one would write
// the value it just read straight back into the model -- marking a note
// modified for the crime of being looked at.
void DrawerPane::fill_task(const core::Node& n) {
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    m_loading = true;

    m_todo.set_active(n.task.is_task);
    m_task_body.set_visible(n.task.is_task);

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
        m_due.remove_css_class("error");
        m_defer.remove_css_class("error");

        // The derived line, spelled out rather than named. "Blocked" alone
        // tells you the verdict and not the reason, and the reason is the only
        // part you can act on.
        std::string why;
        switch (core::availability(*m_src, n.id, now)) {
            case core::Avail::Available: why = "Available now."; break;
            case core::Avail::Done:      why = n.task.done ? "Done."
                                                           : "Inside a finished todo."; break;
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
        m_avail.set_text(why);
    }

    // Children ordering: offered only when there are children to order, or when
    // it is already set (so an existing choice never becomes invisible).
    const bool has_kids = !m_src->children(n.id).empty();
    m_order_row.set_visible(has_kids || n.task.status != core::Status::None);
    switch (n.task.status) {
        case core::Status::Sequential: m_order_seq.set_active(true);  break;
        case core::Status::Parallel:   m_order_par.set_active(true);  break;
        case core::Status::None:       m_order_none.set_active(true); break;
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
    m_order_none.set_sensitive(on);
    m_order_seq.set_sensitive(on);
    m_order_par.set_sensitive(on);
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

void DrawerPane::refresh() {
    const core::NodeId id = m_id;
    show_node(id);
}

void DrawerPane::show_node(const core::NodeId& id) {
    m_id = id;

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
//   * anything else (http, an attachment)   -> the target as written, not clickable
//
// Showing the note's current title rather than the link's label is deliberate:
// a link written as "the grocery thing" pointing at a note since renamed to
// "Shopping" should say Shopping, because you are about to click it and go
// there. The label is kept as the second line so the note's own words survive.
// ─────────────────────────────────────────────────────────────────────────────
void DrawerPane::fill_links(const core::Node& n) {
    const core::Scan sc = core::scan(n.body);
    if (sc.links.empty()) return;

    for (const auto& lk : sc.links) {
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
    set_count(m_links, sc.links.size());
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

// Tags. READ-ONLY -- D4 is open and an editor is a commitment to a storage
// location. What is drawn here is exactly what the body says, deduped.
void DrawerPane::fill_tags(const core::Node& n) {
    const core::Scan sc = core::scan(n.body);
    if (sc.tags.empty()) return;

    std::vector<std::string> names;
    for (const auto& t : sc.tags)
        if (std::find(names.begin(), names.end(), t.name) == names.end())
            names.push_back(t.name);

    std::string line;
    for (const auto& t : names) {
        if (!line.empty()) line += "   ";
        line += "#" + t;
    }
    m_tags.rows->append(*fact_row(line, false));
    set_count(m_tags, names.size());
    m_tags.frame->set_visible(true);
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

void DrawerPane::fill_identity(const core::Node& n) {
    m_uuid.set_text(n.id);
}

}  // namespace jot
