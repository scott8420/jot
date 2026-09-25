#include "EditorPane.hpp"
#include "Log.hpp"

#include "core/Enclosures.hpp"

#include <gdkmm/clipboard.h>
#include <gdkmm/contentformats.h>
#include <gdkmm/texture.h>
#include <glibmm/main.h>
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

Gdk::RGBA rgba(double r, double g, double b) {
    Gdk::RGBA c;
    c.set_rgba(r, g, b, 1.0);
    return c;
}

}  // namespace

EditorPane::EditorPane(std::string_view name)
    : widgets::Box(name, Gtk::Orientation::VERTICAL, 6),
      m_scroll("editor.scroll"),
      m_body("editor.body"),
      m_status("editor.status") {
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
    append(m_scroll);

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
    m_drop = Gtk::DropTarget::create(GDK_TYPE_FILE_LIST, Gdk::DragAction::COPY);
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
    if (auto lg = log::get(log::Area::Editor))
        lg->info("drop: {} file(s) at offset {}", paths.size(), offset);
    m_sig_dropped.emit(paths, offset);
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
    auto buf = m_body.get_buffer();
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
        m_tags.emplace(s, tag);
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

    // .raw() because the scanner works in bytes and Glib::ustring's own
    // iteration is in characters -- taking the bytes here, and taking the
    // codepoint offsets back out of the scan, is the whole of the UTF-8 story
    // on this side of the seam.
    m_scan = core::scan(buf->get_text(/*include_hidden_chars=*/false).raw());

    for (const auto& sp : m_scan.spans) {
        const auto it = m_tags.find(sp.style);
        if (it == m_tags.end()) continue;
        buf->apply_tag(it->second,
                       buf->get_iter_at_offset(sp.cp_begin),
                       buf->get_iter_at_offset(sp.cp_end));
    }
    m_styling = false;
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

}  // namespace jot
