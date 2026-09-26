#include "EditorPane.hpp"
#include "Log.hpp"

#include "core/Enclosures.hpp"

#include <gdkmm/clipboard.h>
#include <gdkmm/device.h>
#include <gdkmm/seat.h>
#include <gdkmm/contentformats.h>
#include <gdkmm/texture.h>
#include <gtkmm/button.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/label.h>
#include <gtkmm/picture.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <glibmm/main.h>
#include <set>
#include <pango/pango-attributes.h>

// EditorPane.cpp -- the note surface. Reads and writes through NodeSource only,
// and gets every styling answer from core::scan.

namespace jot {
namespace {

// Dim grey for syntax marks. A fixed mid-grey ON PURPOSE: the body's own text
// colour comes from whatever theme Appearance::follow_system() landed on, and a
// mid-grey is legible against both ends of that without this file having to
// know which one is current or re-tag the buffer when the desktop flips. If a
// theme ever makes it wrong, the fix is a colour that follows the theme's
// dim-label, not a second tag table.
constexpr double MARK_R = 0.55, MARK_G = 0.55, MARK_B = 0.58;

Gdk::RGBA rgba(double r, double g, double b, double a = 1.0) {
    Gdk::RGBA c;
    c.set_rgba(r, g, b, a);
    return c;
}

// s021b: code's tint. A mid-grey at low alpha, for the same reason MARK is a
// mid-grey: it reads against a light AND a dark theme without knowing which.
constexpr double CODE_A = 0.13;

// The bubble's look, installed once per display. `alpha(currentColor, ..)`
// follows the theme's text colour, so the bubble is a shade darker than the
// page in light mode and a shade lighter in dark mode.
void install_code_css() {
    static bool done = false;
    if (done) return;
    auto display = Gdk::Display::get_default();
    if (!display) return;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        ".jot-codeblock { background-color: alpha(currentColor, 0.07);"
        "                 border-radius: 8px; padding: 4px 6px 10px 12px; }"
        ".jot-codeblock .jot-code { font-family: monospace; }"
        ".jot-codeblock .jot-code-lang { font-size: smaller; opacity: 0.6; }");
    gtk_style_context_add_provider_for_display(display->gobj(), GTK_STYLE_PROVIDER(css->gobj()),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    done = true;
}

}  // namespace

EditorPane::EditorPane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL, 6),
      m_scroll("editor.scroll"),
      m_body("editor.body"),
      m_status("editor.status"),
      m_stack("editor.stack"),
      m_read_scroll("editor.read_scroll"),
      m_read("editor.read") {
    set_margin(12);

    m_body.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    // NOT monospace any more, and that is the visible half of this milestone.
    // s003 set it because the body showed markdown as source and source wants a
    // fixed pitch. The body is now prose that happens to carry marks, so it
    // gets the reading font and `code` gets monospace back via its own tag.
    m_body.set_left_margin(10);
    m_body.set_right_margin(10);
    m_body.set_top_margin(8);
    m_body.set_bottom_margin(8);
    m_body.set_pixels_below_lines(2);
    m_scroll.set_child(m_body);
    m_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    m_scroll.set_has_frame(true);

    // ── the reading view (s021) ─────────────────────────────────────────────
    // A second TextView, never editable, with the same margins and the same
    // tag table, so a heading is the same size in both and flipping between
    // them does not make the note jump. Selectable: copying from a note you
    // are reading is the most ordinary thing to want.
    m_read.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    m_read.set_left_margin(10);
    m_read.set_right_margin(10);
    m_read.set_top_margin(8);
    m_read.set_bottom_margin(8);
    m_read.set_pixels_below_lines(4);
    m_read.set_editable(false);
    m_read.set_cursor_visible(false);
    m_read_scroll.set_child(m_read);
    m_read_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    m_read_scroll.set_vexpand(true);
    m_read_scroll.set_has_frame(true);

    m_stack.add(m_scroll, "source");
    m_stack.add(m_read_scroll, "reading");
    m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_stack.set_transition_duration(120);
    m_stack.set_vexpand(true);
    append(m_stack);

    m_read_click = Gtk::GestureClick::create();
    m_read_click->set_button(GDK_BUTTON_PRIMARY);
    m_read_click->signal_released().connect(
        [this](int n, double x, double y) { on_read_click(n, x, y); });
    m_read.add_controller(m_read_click);
    m_read_motion = Gtk::EventControllerMotion::create();
    m_read_motion->signal_motion().connect([this](double x, double y) { on_read_motion(x, y); });
    m_read.add_controller(m_read_motion);

    // ── the status line, RETIRED ────────────────────────────────────────────
    // It existed to prove by looking that a drag preserved identity instead of
    // minting a new id the way Notr's did. That test has been run and it held,
    // and everything else it carried -- the parent, the child count, the
    // protected state -- is now in the drawer, where it has room to say the
    // parent's TITLE instead of its id.
    //
    // What survives is the blank state and only the blank state: an editor
    // showing nothing, with no explanation, looks broken rather than empty. It
    // is hidden the moment a note is shown.
    m_status.set_xalign(0.0f);
    m_status.add_css_class("dim-label");
    m_status.set_wrap(true);
    append(m_status);

    build_tags();

    m_body.get_buffer()->signal_changed().connect([this]() {
        write_body();
        queue_restyle();
    });

    // Click-to-tick. Attached to the TextView and deliberately NOT claiming the
    // event: a click that misses a checkbox must still place the cursor, which
    // is what a text view's own handler is for.
    m_click = Gtk::GestureClick::create();
    m_click->set_button(GDK_BUTTON_PRIMARY);
    m_click->signal_released().connect(
        [this](int n, double x, double y) { on_body_click(n, x, y); });
    m_body.add_controller(m_click);

    // ── a drop of files (s016b) ─────────────────────────────────────────────
    // CAPTURE phase, on the text view itself: GtkTextView has its own drop
    // target for text, in the bubble phase, and a drag from Files offers the
    // file list AS text too (the uri). Heard second, a dropped photo would
    // arrive as the string "file:///home/..." typed into the note. Heard
    // first, it arrives as a file. A drop that holds no image is refused and
    // nothing is inserted -- non-image enclosures are a later milestone.
    // s019: every action, so a modified drag is not refused and we can SEE
    // which one arrived. Scott's log: on GNOME Wayland a held Ctrl+Shift never
    // reaches jot -- Wayland has no LINK action, and Files holds the keyboard.
    // What the compositor does pass is the drag ACTION (Shift -> MOVE). A
    // MOVE or ASK is read as "the other mode", and is never FINISHED as a
    // move: see kAccept and on_drop.
    m_drop = Gtk::DropTarget::create(GDK_TYPE_FILE_LIST, kAccept);
    m_drop->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    m_drop->signal_drop().connect(
        [this](const Glib::ValueBase& v, double x, double y) { return on_drop(v, x, y); },
        false);
    m_body.add_controller(m_drop);

    // ── a paste of a picture (s016b) ────────────────────────────────────────
    // "paste-clipboard" is the keybinding signal Ctrl+V AND the context menu
    // both emit, so hooking it covers both. Connected with the C API because
    // gtkmm 4.10 does not wrap it; run before the default handler, which it
    // stops only when the clipboard holds an image and NO text.
    g_signal_connect(m_body.gobj(), "paste-clipboard", G_CALLBACK(&EditorPane::paste_trampoline),
                     this);

    show_node("");
}

// ─────────────────────────────────────────────────────────────────────────────
// Enclosures in. Recognise, locate, emit -- nothing here writes a file.
// ─────────────────────────────────────────────────────────────────────────────
const Gdk::DragAction EditorPane::kAccept = Gdk::DragAction::COPY | Gdk::DragAction::MOVE |
                                             Gdk::DragAction::LINK | Gdk::DragAction::ASK;

bool EditorPane::on_drop(const Glib::ValueBase& value, double x, double y) {
    if (!m_body.get_editable() || m_id.empty()) return false;
    auto* list = static_cast<GdkFileList*>(g_value_get_boxed(value.gobj()));
    if (!list) return false;

    std::vector<std::string> paths;
    GSList* files = gdk_file_list_get_files(list);
    for (GSList* l = files; l; l = l->next) {
        char* p = g_file_get_path(G_FILE(l->data));
        if (p) paths.emplace_back(p);   // any file (s018); a folder is refused by the ingest, aloud
        g_free(p);
    }
    g_slist_free(files);

    if (paths.empty()) {
        if (auto lg = log::get(log::Area::Editor))
            lg->info("drop refused: nothing local among the dropped files");
        return false;
    }

    int bx = 0, by = 0;
    m_body.window_to_buffer_coords(Gtk::TextWindowType::WIDGET, static_cast<int>(x),
                                   static_cast<int>(y), bx, by);
    Gtk::TextIter it;
    m_body.get_iter_at_location(it, bx, by);
    const int offset = it.get_offset();

    // s019: did the drop ask for the OTHER mode? Scott's log (s019c) settled
    // it for GNOME Wayland: only the ACTION witness below (a Shift-drag
    // arrives as MOVE, raw=2) ever fires. The first three are kept for X11,
    // where GTK's own Ctrl+Shift = LINK convention does reach us:
    //   the drop event's modifiers, the keyboard's modifiers right now, and
    //   the action the source settled on (LINK and not COPY).
    const auto kBoth = Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK;
    const auto ev_state = m_drop->get_current_event_state();
    const bool by_event = (ev_state & kBoth) == kBoth;
    bool by_keyboard = false;
    if (auto display = m_body.get_display())
        if (auto seat = display->get_default_seat())
            if (auto kb = seat->get_keyboard())
                by_keyboard = (kb->get_modifier_state() & kBoth) == kBoth;
    bool by_action = false;
    if (auto drop = m_drop->get_current_drop()) {
        const auto a = drop->get_actions();
        by_action = (a & Gdk::DragAction::LINK) == Gdk::DragAction::LINK &&
                    (a & Gdk::DragAction::COPY) != Gdk::DragAction::COPY;
    }
    // s019c: a MOVE or an ASK is a modified drag too. The raw action is
    // logged as bits (1 copy, 2 move, 4 link, 8 ask) so a drag nobody here
    // can make still says what it was.
    int raw = -1;
    bool by_move = false;
    if (auto drop = m_drop->get_current_drop()) {
        const auto a = drop->get_actions();
        raw = static_cast<int>(a);
        by_move = (a & Gdk::DragAction::COPY) != Gdk::DragAction::COPY &&
                  ((a & Gdk::DragAction::MOVE) == Gdk::DragAction::MOVE ||
                   (a & Gdk::DragAction::ASK) == Gdk::DragAction::ASK);
        // NEVER let the source believe it was moved: GTK finishes the drop
        // with (our actions & its actions), so narrowing ours to COPY makes a
        // MOVE finish as "nothing" and Files deletes nothing. Restored on an
        // idle, after GTK has finished this drop.
        if (by_move) {
            m_drop->set_actions(Gdk::DragAction::COPY);
            Glib::signal_idle().connect_once([this]() { m_drop->set_actions(kAccept); });
        }
    }
    const bool flip = by_event || by_keyboard || by_action || by_move;
    if (auto lg = log::get(log::Area::Editor))
        lg->info("drop: {} file(s) at offset {}; flip={} (event={} keyboard={} action={} "
                 "move={} raw={})",
                 paths.size(), offset, flip, by_event, by_keyboard, by_action, by_move, raw);
    m_sig_dropped.emit(paths, offset, flip);
    return true;
}

void EditorPane::paste_trampoline(GtkTextView*, gpointer self) {
    static_cast<EditorPane*>(self)->on_paste_clipboard();
}

void EditorPane::on_paste_clipboard() {
    if (!m_body.get_editable() || m_id.empty()) return;
    auto clip = m_body.get_clipboard();
    auto formats = clip ? clip->get_formats() : Glib::RefPtr<Gdk::ContentFormats>{};
    if (!formats) return;
    // Text wins whenever there is text. An app that copies a selection often
    // offers a picture of it as well (office suites do), and the user copied
    // WORDS. Only a clipboard that is a picture and nothing readable becomes
    // an enclosure -- a screenshot, a browser's Copy Image.
    const bool image = formats->contain_gtype(GDK_TYPE_TEXTURE);
    const bool text  = formats->contain_gtype(G_TYPE_STRING);
    if (!image || text) return;

    g_signal_stop_emission_by_name(m_body.gobj(), "paste-clipboard");
    const int offset = cursor_offset();
    const core::NodeId id = m_id;
    if (auto lg = log::get(log::Area::Editor)) lg->info("paste: an image, at offset {}", offset);

    clip->read_texture_async([this, clip, offset, id](Glib::RefPtr<Gio::AsyncResult>& r) {
        Glib::RefPtr<Gdk::Texture> tex;
        try {
            tex = clip->read_texture_finish(r);
        } catch (const Glib::Error& e) {
            if (auto lg = log::get(log::Area::Editor))
                lg->warn("paste: the clipboard image could not be read: {}", e.what());
            return;
        }
        // The note may have changed while the clipboard answered. An image
        // pasted into one note must not land in the next one you clicked.
        if (!tex || id != m_id) return;
        auto bytes = tex->save_to_png_bytes();
        if (!bytes) return;
        gsize n = 0;
        const auto* data = static_cast<const char*>(bytes->get_data(n));
        m_sig_pasted.emit(std::string(data, n), offset);
    }, Glib::RefPtr<Gio::Cancellable>{});
}

int EditorPane::cursor_offset() const {
    auto buf = const_cast<Gtk::TextView&>(static_cast<const Gtk::TextView&>(m_body)).get_buffer();
    return buf->get_insert()->get_iter().get_offset();
}

bool EditorPane::insert_block(int cp_offset, const std::string& text) {
    if (!m_body.get_editable() || m_id.empty() || text.empty()) return false;
    auto buf = m_body.get_buffer();
    auto it = buf->get_iter_at_offset(cp_offset);
    std::string block = text;
    if (!it.starts_line()) block = "\n" + block;
    if (!it.ends_line()) block += "\n";
    it = buf->insert(it, block);
    buf->place_cursor(it);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// build_tags -- the tag table, created ONCE, in core::all_styles() order.
//
// The order is load-bearing. GtkTextTag priority is creation order and later
// wins, and core::all_styles() puts Mark last precisely so a dimmed asterisk
// inside a bold run stays dimmed. Iterating that list rather than writing the
// tags out by hand is what keeps the scan and the table from drifting: a style
// added to the enum without a case below fails to compile.
// ─────────────────────────────────────────────────────────────────────────────
void EditorPane::build_tags() {
    make_tags(m_body.get_buffer(), m_tags);
    make_tags(m_read.get_buffer(), m_read_tags);   // s021: one table, two buffers
    // s021b: in SOURCE, a fenced block's lines -- fences included -- sit on
    // one full-width tint, so the block is a block while you edit it. The
    // lines INSIDE get this tag INSTEAD of Code (see restyle): Code carries
    // the inline tint, and stacked on the paragraph tint it doubled every
    // glyph's shade. (A "transparent" text background to cancel it drew
    // BLACK under GTK 4.14 -- seen under Xvfb, s021b.)
    m_codeblock_tag = m_body.get_buffer()->create_tag("md-codeblock");
    m_codeblock_tag->property_paragraph_background_rgba() = rgba(0.5, 0.5, 0.5, CODE_A);
    m_codeblock_tag->property_family() = "monospace";
    m_codeblock_tag->property_scale() = 0.92;
}

void EditorPane::make_tags(const Glib::RefPtr<Gtk::TextBuffer>& buf,
                           std::map<core::Style, Glib::RefPtr<Gtk::TextTag>>& out) {
    for (const core::Style s : core::all_styles()) {
        auto tag = buf->create_tag(core::tag_name(s));
        switch (s) {
            case core::Style::Mark:
                tag->property_foreground_rgba() = rgba(MARK_R, MARK_G, MARK_B);
                break;
            // Headings scale rather than name a point size, so they stay
            // proportional to whatever the user's document font is.
            case core::Style::H1:
                tag->property_scale() = PANGO_SCALE_XX_LARGE;
                tag->property_weight() = Pango::Weight::BOLD;
                tag->property_pixels_above_lines() = 14;
                break;
            case core::Style::H2:
                tag->property_scale() = PANGO_SCALE_X_LARGE;
                tag->property_weight() = Pango::Weight::BOLD;
                tag->property_pixels_above_lines() = 12;
                break;
            case core::Style::H3:
                tag->property_scale() = PANGO_SCALE_LARGE;
                tag->property_weight() = Pango::Weight::BOLD;
                tag->property_pixels_above_lines() = 10;
                break;
            case core::Style::H4:
            case core::Style::H5:
            case core::Style::H6:
                tag->property_weight() = Pango::Weight::BOLD;
                tag->property_pixels_above_lines() = 8;
                break;
            case core::Style::Bold:
                tag->property_weight() = Pango::Weight::BOLD;
                break;
            case core::Style::Italic:
                tag->property_style() = Pango::Style::ITALIC;
                break;
            case core::Style::Code:
                tag->property_family() = "monospace";
                tag->property_scale() = 0.92;
                // s021b: inline `code` sits on a faint tint, like everywhere
                // else markdown is read.
                tag->property_background_rgba() = rgba(0.5, 0.5, 0.5, CODE_A);
                break;
            case core::Style::Strike:
                tag->property_strikethrough() = true;
                tag->property_foreground_rgba() = rgba(MARK_R, MARK_G, MARK_B);
                break;
            case core::Style::Link:
                tag->property_underline() = Pango::Underline::SINGLE;
                tag->property_foreground_rgba() = rgba(0.26, 0.52, 0.88);
                break;
            case core::Style::Tag:
                // A tag reads as a label, not as a link: no underline, because
                // clicking one does nothing yet. D4 is open, so the body is the
                // only place a tag lives and the drawer only reports what it
                // finds here. When a tag becomes clickable this gets the
                // underline and that will be the visible half of the answer.
                tag->property_foreground_rgba() = rgba(0.42, 0.56, 0.36);
                tag->property_weight() = Pango::Weight::SEMIBOLD;
                break;
            case core::Style::Quote:
                tag->property_style() = Pango::Style::ITALIC;
                tag->property_left_margin() = 28;
                tag->property_foreground_rgba() = rgba(MARK_R, MARK_G, MARK_B);
                break;
            case core::Style::TaskDone:
                // A done task dims and strikes rather than disappearing. The
                // whole reason tasks live inside prose in jot is that the prose
                // is the record; a finished line still has to be readable.
                tag->property_strikethrough() = true;
                tag->property_foreground_rgba() = rgba(MARK_R, MARK_G, MARK_B);
                break;
            case core::Style::Rule:
                // A `---` line reads as an actual rule when it is struck
                // through: three hyphens with a line across them is a line.
                tag->property_strikethrough() = true;
                break;
        }
        out.emplace(s, tag);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// restyle -- rescan the whole body and re-apply every tag.
//
// WHOLE BUFFER, not the edited line, and that is not laziness: a fence opened
// on line 3 changes what every line below it is, so there is no correct
// line-local markdown restyle. The selftest holds the scan to 14ms on a
// 16000-line body, which is far past any note, so the honest cheap thing is to
// do all of it.
// ─────────────────────────────────────────────────────────────────────────────
void EditorPane::restyle() {
    auto buf = m_body.get_buffer();
    if (!buf) return;

    m_styling = true;
    const auto b = buf->begin(), e = buf->end();
    for (const auto& [style, tag] : m_tags) buf->remove_tag(tag, b, e);
    if (m_codeblock_tag) buf->remove_tag(m_codeblock_tag, b, e);

    // .raw() because the scanner works in bytes and Glib::ustring's own
    // iteration is in characters -- taking the bytes here, and taking the
    // codepoint offsets back out of the scan, is the whole of the UTF-8 story
    // on this side of the seam.
    m_scan = core::scan(buf->get_text(/*include_hidden_chars=*/false).raw());

    // s021b: a code-block line is styled by md-codeblock below, not by Code.
    std::set<int> block_lines;
    for (const auto& ln : m_scan.lines)
        if (ln.block == core::Block::Code) block_lines.insert(ln.cp_begin);
    for (const auto& sp : m_scan.spans) {
        if (sp.style == core::Style::Code && block_lines.count(sp.cp_begin)) continue;
        const auto it = m_tags.find(sp.style);
        if (it == m_tags.end()) continue;
        buf->apply_tag(it->second,
                       buf->get_iter_at_offset(sp.cp_begin),
                       buf->get_iter_at_offset(sp.cp_end));
    }
    if (m_codeblock_tag)
        for (const auto& ln : m_scan.lines)
            if (ln.block == core::Block::Fence || ln.block == core::Block::Code) {
                auto lb = buf->get_iter_at_offset(ln.cp_begin);
                auto le = lb;
                le.forward_line();   // the newline too, so the tint spans the whole line
                buf->apply_tag(m_codeblock_tag, lb, le);
            }
    m_styling = false;
    if (m_reading) render_reading();   // s021: the view follows the source
}

// Coalesce to one restyle per idle. Typing fires `changed` per keystroke, and
// restyling on each of them would put a whole-buffer scan in the keystroke
// path. This is the shape Folio's sp_auto_sense uses, and the reason it uses
// it.
void EditorPane::queue_restyle() {
    if (m_restyle_queued || m_styling) return;
    m_restyle_queued = true;
    Glib::signal_idle().connect_once([this]() {
        m_restyle_queued = false;
        restyle();
    });
}

void EditorPane::set_source(core::NodeSource* src) {
    m_src = src;
    show_node("");
}

void EditorPane::set_editable(bool on) {
    m_body.set_editable(on);
    m_body.set_cursor_visible(on);
}

void EditorPane::show_node(const core::NodeId& id) {
    m_loading = true;
    m_id = id;

    const core::Node* n = (m_src && !id.empty()) ? m_src->find(id) : nullptr;
    if (!n) {
        m_id.clear();
        m_body.get_buffer()->set_text("");
        m_status.set_text("No note selected. Pick one on the left, or press "
                          "Ctrl+N to start a new one.");
        m_status.set_visible(true);
        set_editable(false);
        m_loading = false;
        restyle();          // clears m_scan, so a stale click can't find a box
        return;
    }

    m_body.get_buffer()->set_text(n->body);
    set_editable(!n->protect);

    // Protection is the one fact the drawer carries that ALSO changes what this
    // pane lets you do, so it needs a tell that survives the drawer being
    // hidden -- and in full focus mode, with both side panes gone, this line is
    // the ONLY surface left. A read-only note whose only tell is that typing
    // does nothing is a bug report waiting to happen.
    //
    // It rides on the retired status line, which exists for the blank state and
    // was already the right shape: one dim line under the body that is there
    // when there is something to say and gone when there is not.
    if (n->protect) {
        m_status.set_text("Protected \u2014 read-only");
        m_status.set_visible(true);
    } else {
        m_status.set_visible(false);
    }

    m_loading = false;

    // Immediately, not queued: switching notes must not show one frame of
    // unstyled source before the idle runs.
    restyle();
}

// A new note exists to be typed into. Anything that makes the user click before
// they can write is a longer capture path than a paper scrap has.
void EditorPane::focus_capture() {
    // s021: a capture means typing, and Reading cannot be typed into. Ask
    // for Source first -- the Shell owns the mode.
    if (m_reading) m_sig_edit.emit(-1);
    if (m_body.get_editable()) m_body.grab_focus();
}

void EditorPane::refresh() {
    // The status line and the protected state are the parts an outside change
    // (a drag, a protect toggle) can invalidate. Re-reading the whole node is
    // the same cost and can't fall out of step with the model.
    const core::NodeId id = m_id;
    show_node(id);
}

void EditorPane::write_body() {
    if (m_loading || m_styling || !m_src || m_id.empty()) return;
    auto buf = m_body.get_buffer();
    m_src->set_body(m_id, buf->get_text(/*include_hidden_chars=*/false));
}

// ─────────────────────────────────────────────────────────────────────────────
// on_body_click -- tick a checkbox by clicking it.
//
// This is the first place jot is a todo app rather than a notes app, and it is
// three lines of gesture plus the scan we already had. The hit area is the
// three characters of the box itself and nothing wider: a task line's text is
// text, and clicking it has to put a cursor there like any other line.
// ─────────────────────────────────────────────────────────────────────────────
void EditorPane::on_body_click(int n_press, double x, double y) {
    if (n_press != 1 || !m_body.get_editable()) return;

    int bx = 0, by = 0;
    m_body.window_to_buffer_coords(Gtk::TextWindowType::WIDGET,
                                   static_cast<int>(x), static_cast<int>(y), bx, by);
    Gtk::TextBuffer::iterator it;
    int trailing = 0;
    if (!m_body.get_iter_at_position(it, trailing, bx, by)) return;

    toggle_task_at(it.get_line(), it.get_line_offset());
}

bool EditorPane::toggle_task_at(int line, int cp_offset) {
    if (line < 0 || line >= static_cast<int>(m_scan.lines.size())) return false;
    const core::Line& ln = m_scan.lines[static_cast<std::size_t>(line)];
    if (ln.block != core::Block::Task || ln.cp_box_begin < 0) return false;

    // cp_box_begin is an offset into the whole buffer; get_line_offset() is an
    // offset into the line. Comparing the two directly would put the hit area
    // on a different line the moment the note has more than one -- convert.
    const int box_in_line     = ln.cp_box_begin - ln.cp_begin;
    const int box_in_line_end = ln.cp_box_end   - ln.cp_begin;
    if (cp_offset < box_in_line || cp_offset >= box_in_line_end) return false;

    auto buf = m_body.get_buffer();
    const int state_cp = ln.cp_box_begin + 1;     // the character between [ and ]
    auto s = buf->get_iter_at_offset(state_cp);
    auto e = buf->get_iter_at_offset(state_cp + 1);

    // Deliberately NOT guarded by m_styling: this edit SHOULD reach the store
    // and SHOULD trigger a restyle, exactly as if it had been typed. Ticking a
    // box is a note edit, not a view state.
    buf->erase(s, e);
    buf->insert(buf->get_iter_at_offset(state_cp), ln.checked ? " " : "x");

    if (auto lg = log::get(log::Area::Editor))
        lg->debug("task toggled: note={} line={} -> {}", m_id, line,
                  ln.checked ? "open" : "done");

    // The scan in hand is now one character out of date. It is rebuilt on the
    // idle that the edit above just queued; until then, refuse further hits
    // rather than act on stale offsets.
    m_scan.lines[static_cast<std::size_t>(line)].cp_box_begin = -1;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The reading view (s021). core::render decides what it says; this only puts
// it in a buffer, swaps each U+FFFC for a real child anchor (one codepoint for
// one codepoint, so no offset moves), and answers clicks.
// ─────────────────────────────────────────────────────────────────────────────
void EditorPane::set_reading(bool on, int source_cp) {
    if (on == m_reading && source_cp < 0) return;
    m_reading = on;
    auto body = m_body.get_buffer();
    if (on) {
        render_reading();
        m_stack.set_visible_child("reading");
        // Keep your place: the source cursor's line, found in the render.
        const int at = core::rendered_cp(m_rendered, body->get_insert()->get_iter().get_offset());
        Glib::signal_idle().connect_once([this, at]() {
            auto rb = m_read.get_buffer();
            auto it = rb->get_iter_at_offset(at);
            m_read.scroll_to(it, 0.2);
        });
    } else {
        m_stack.set_visible_child("source");
        if (source_cp >= 0) body->place_cursor(body->get_iter_at_offset(source_cp));
        Glib::signal_idle().connect_once([this]() {
            m_body.scroll_to(m_body.get_buffer()->get_insert(), 0.2);
            if (m_body.get_editable()) m_body.grab_focus();
        });
    }
    if (auto lg = log::get(log::Area::Editor))
        lg->info("view: {} (note={})", on ? "reading" : "source", m_id);
}

void EditorPane::render_reading() {
    auto body = m_body.get_buffer();
    auto rb   = m_read.get_buffer();
    const double keep = m_read_scroll.get_vadjustment()->get_value();

    m_rendered = core::render(body->get_text(false).raw());
    rb->set_text(m_rendered.text);
    for (const auto& sp : m_rendered.spans) {
        const auto it = m_read_tags.find(sp.style);
        if (it == m_read_tags.end()) continue;
        rb->apply_tag(it->second, rb->get_iter_at_offset(sp.cp_begin),
                      rb->get_iter_at_offset(sp.cp_end));
    }

    // Anchors from the END, so replacing one never disturbs the next. Each
    // is a placeholder deleted and an anchor created in the same place.
    for (auto a = m_rendered.anchors.rbegin(); a != m_rendered.anchors.rend(); ++a) {
        auto it = rb->get_iter_at_offset(a->cp);
        if (it.get_char() != 0xFFFC) continue;   // a mismatch: leave it, never guess
        auto next = it;
        next.forward_char();
        it = rb->erase(it, next);
        auto anchor = rb->create_child_anchor(it);

        Gtk::Widget* w = nullptr;
        if (a->kind == core::RenderAnchor::Kind::Code) {
            w = code_bubble(a->code, a->lang);
        } else if (a->kind == core::RenderAnchor::Kind::Rule) {
            auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
            sep->set_size_request(420, -1);
            sep->set_margin_top(8);
            sep->set_margin_bottom(8);
            w = sep;
        } else {
            const std::string path = m_resolve ? m_resolve(a->target) : std::string{};
            Glib::RefPtr<Gdk::Texture> tex;
            if (!path.empty()) {
                try {
                    tex = Gdk::Texture::create_from_file(Gio::File::create_for_path(path));
                } catch (const Glib::Error&) {
                }
            }
            if (tex) {
                // Fit a column, never enlarge: a 4000-px photo becomes 520
                // wide; an icon stays an icon.
                constexpr int kMaxW = 520;
                const int tw = tex->get_width(), th = tex->get_height();
                const int w0 = std::min(tw, kMaxW);
                const int h0 = tw > 0 ? th * w0 / tw : th;
                auto* pic = Gtk::make_managed<Gtk::Picture>(tex);
                pic->set_can_shrink(true);
                pic->set_content_fit(Gtk::ContentFit::CONTAIN);
                pic->set_size_request(w0, h0);
                pic->set_tooltip_text(a->label.empty() ? a->target : a->label);
                w = pic;
            } else {
                // Said, not dropped: a picture the note names and jot cannot
                // show is a fact about the note.
                auto* lab = Gtk::make_managed<Gtk::Label>(
                    "\U0001F5BC " + (a->label.empty() ? a->target : a->label) + " (not found)");
                lab->add_css_class("dim-label");
                w = lab;
            }
        }
        m_read.add_child_at_anchor(*w, anchor);
    }

    Glib::signal_idle().connect_once(
        [this, keep]() { m_read_scroll.get_vadjustment()->set_value(keep); });
}

int EditorPane::read_offset_at(double x, double y) {
    int bx = 0, by = 0;
    m_read.window_to_buffer_coords(Gtk::TextWindowType::WIDGET, static_cast<int>(x),
                                   static_cast<int>(y), bx, by);
    Gtk::TextBuffer::iterator it;
    int trailing = 0;
    if (!m_read.get_iter_at_position(it, trailing, bx, by)) return -1;
    return it.get_offset();
}

void EditorPane::on_read_click(int n_press, double x, double y) {
    const int off = read_offset_at(x, y);
    if (off < 0) return;
    // A double-click is "edit here" -- asked BEFORE the selection test,
    // because the view's own double-click has just selected the word.
    if (n_press == 2) {
        auto rb = m_read.get_buffer();
        rb->place_cursor(rb->get_iter_at_offset(off));   // drop the word selection
        m_sig_edit.emit(core::source_cp(m_rendered, off));
        return;
    }
    // A selection made by dragging is not a click on what it ended over.
    if (m_read.get_buffer()->get_has_selection()) return;

    if (n_press == 1) {
        for (const auto& bx : m_rendered.boxes) {
            if (off != bx.cp) continue;
            if (!m_body.get_editable()) return;   // protected: the box is a picture of a box
            if (bx.line >= static_cast<int>(m_scan.lines.size())) return;
            const auto& ln = m_scan.lines[static_cast<std::size_t>(bx.line)];
            if (ln.cp_box_begin >= 0) toggle_task_at(bx.line, ln.cp_box_begin - ln.cp_begin);
            return;
        }
        for (const auto& lk : m_rendered.links)
            if (off >= lk.cp_begin && off < lk.cp_end) {
                if (auto lg = log::get(log::Area::Editor)) lg->info("reading: follow {}", lk.target);
                m_sig_link.emit(lk.target);
                return;
            }
        return;
    }
}

void EditorPane::on_read_motion(double x, double y) {
    const int off = read_offset_at(x, y);
    bool hot = false;
    if (off >= 0) {
        for (const auto& bx : m_rendered.boxes) hot = hot || off == bx.cp;
        for (const auto& lk : m_rendered.links) hot = hot || (off >= lk.cp_begin && off < lk.cp_end);
    }
    // The C call: gtkmm 4.10 does not wrap set_cursor_from_name.
    gtk_widget_set_cursor_from_name(GTK_WIDGET(m_read.gobj()), hot ? "pointer" : "text");
}

// s021b -- a fenced block in Reading: a rounded tint, the language (if the
// fence named one) and a Copy button top-right, then the code, monospace,
// selectable, never wrapped (a wrapped line of code is a different line of
// code), scrolling sideways if it is wide. Copy puts EXACTLY the text between
// the fences on the clipboard -- no backticks, no trailing newline.
Gtk::Widget* EditorPane::code_bubble(const std::string& code, const std::string& lang) {
    install_code_css();
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    box->add_css_class("jot-codeblock");
    box->set_name("editor.code_bubble");
    // As wide as the page, so every bubble has the same right edge.
    const int page = m_read.get_width();
    box->set_size_request(page > 200 ? page - 40 : 560, -1);

    auto* head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto* lang_l = Gtk::make_managed<Gtk::Label>(lang);
    lang_l->add_css_class("jot-code-lang");
    lang_l->set_xalign(0.0f);
    lang_l->set_hexpand(true);
    head->append(*lang_l);

    auto* copy = Gtk::make_managed<Gtk::Button>();
    copy->set_name("editor.code_copy");
    copy->set_icon_name("edit-copy-symbolic");
    copy->set_has_frame(false);
    copy->set_tooltip_text("Copy code");
    copy->signal_clicked().connect([this, copy, code]() {
        m_read.get_clipboard()->set_text(code);
        if (auto lg = log::get(log::Area::Editor))
            lg->info("reading: code copied ({} byte(s))", code.size());
        // Say it worked, then go back. The button is held for the timeout:
        // a re-render in the meantime would otherwise free it under us.
        copy->set_icon_name("object-select-symbolic");
        copy->set_tooltip_text("Copied");
        copy->reference();
        Glib::signal_timeout().connect_once(
            [copy]() {
                copy->set_icon_name("edit-copy-symbolic");
                copy->set_tooltip_text("Copy code");
                copy->unreference();
            },
            1500);
    });
    head->append(*copy);
    box->append(*head);

    auto* text = Gtk::make_managed<Gtk::Label>(code);
    text->add_css_class("jot-code");
    text->set_xalign(0.0f);
    text->set_selectable(true);
    text->set_wrap(false);
    auto* sc = Gtk::make_managed<Gtk::ScrolledWindow>();
    sc->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::NEVER);
    sc->set_propagate_natural_height(true);
    sc->set_child(*text);
    box->append(*sc);
    return box;
}

}  // namespace jot
