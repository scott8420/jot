#pragma once
#include "core/Markdown.hpp"
#include "core/Nodes.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/droptarget.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/texttag.h>
#include <map>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// EditorPane -- the note itself: a title entry and a markdown body that styles
// itself as you type.
//
// s004 made this real. The body is no longer a monospace dump of source: a
// heading is large, a bold run is bold, a checked task is struck through, and a
// checkbox ticks when you click it. What it is NOT is a WYSIWYG editor. The
// syntax marks stay on screen, dimmed -- `## Heading` keeps its hashes.
//
// That was Scott's call (s004) and it buys more than a look. The buffer stays
// byte-identical to the file on disk, so there is no hidden text, no
// cursor-arrives-reveal-it rule, no second coordinate space, and TextMap never
// enters the editor. Every offset in the buffer is an offset in the note. The
// Obsidian-style hiding variant is this plus a reveal rule and can be built on
// top of it later without undoing anything here.
//
// D5 is ANSWERED (s004): Folio's markdown editor does not lift, because Folio
// has no markdown editor. Its Editor is ~13k lines of WYSIWYG over a
// GtkTextBuffer serialized to HTML; markdown appears only at its import/export
// boundary, with no inline parsing in either direction. What DID lift is the
// discipline around its screenplay auto-sense: restyle on IDLE rather than per
// keystroke, behind a re-entry guard, and never fight what the user typed.
// Those three ideas are the whole of what this file borrowed, and they were
// worth reading 13k lines to find.
//
// The division of labour: core::scan answers "what is this text" with no
// display and under selftest; this class only turns that answer into tags. A
// styling bug is therefore either a scan bug (visible in jot_selftest) or a
// tag bug (visible on screen), and never a mystery in between.
//
// Write-through to core::NodeSource is unchanged from s003: every keystroke
// goes to the store, which owns its own flush policy.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

class EditorPane : public widgets::Box {
public:
    explicit EditorPane(std::string_view name);

    void set_source(core::NodeSource* src);
    void show_node(const core::NodeId& id);   // empty id => the blank state
    void refresh();                           // re-read the current node after an outside change
    // The capture path: a new note must be typing-ready with no click. That is
    // the BODY now -- see the note on the title entry below. Capture first,
    // name second; putting the cursor in a name field would be the toll gate
    // ARCHITECTURE warns about, wearing a smaller hat.
    void focus_capture();
    core::NodeId current() const { return m_id; }

    // ── enclosures in (s016b) ───────────────────────────────────────────────
    // The editor RECOGNISES an image arriving -- a drop of files from Files, or
    // a paste whose clipboard holds a picture and no text -- and says so with
    // the buffer offset it arrived at. It does not copy anything or choose a
    // name: where an attachment lives depends on whether there is a jots
    // folder yet, and that is the Shell's knowledge, not the note's.
    //
    // Local paths of the dropped files, and where. ANY file since s018 -- the
    // kind (image or not) is decided when the reference is written, not here.
    sigc::signal<void(std::vector<std::string>, int)>& signal_files_dropped() {
        return m_sig_dropped;
    }
    // PNG bytes of a pasted picture, and where.
    sigc::signal<void(std::string, int)>& signal_image_pasted() { return m_sig_pasted; }

    // Put text into the body at a codepoint offset, as if typed there: it goes
    // through the ordinary `changed` -> write-through path, so the store, the
    // drawer and the index hear about it the usual way. Block-shaped: a
    // newline is added before when the offset is mid-line and after when text
    // follows, so an image reference always sits on its own line. Refused on
    // a protected note. Returns false if nothing was inserted.
    bool insert_block(int cp_offset, const std::string& text);

    // The cursor's offset, for a paste (which lands at the cursor).
    int cursor_offset() const;

private:
    bool on_drop(const Glib::ValueBase& value, double x, double y);
    void on_paste_clipboard();       // "paste-clipboard", run BEFORE the default
    static void paste_trampoline(GtkTextView*, gpointer self);

    void write_body();
    void set_editable(bool on);

    void build_tags();                        // the tag table, once
    void restyle();                           // rescan the whole body and re-tag it
    void queue_restyle();                     // coalesce to one restyle per idle
    void on_body_click(int n_press, double x, double y);
    bool toggle_task_at(int line, int cp_offset);

    core::NodeSource* m_src = nullptr;   // not owned; the seam
    core::NodeId      m_id;

    // NO TITLE ENTRY. It was here from s002 and s006 gave the note THREE ways
    // to be renamed -- this entry, a Name field in the drawer, and double-
    // clicking a tree row. Scott cut this one: the middle pane is the note, and
    // a name is metadata about the note. The drawer is where metadata lives and
    // the tree is where you are already pointing when you want to rename
    // something. A third copy of the same field in the one pane that is
    // supposed to be just the writing surface was the odd one out.
    widgets::ScrolledWindow m_scroll;
    widgets::TextView       m_body;
    widgets::Label          m_status;

    Glib::RefPtr<Gtk::GestureClick> m_click;
    Glib::RefPtr<Gtk::DropTarget>   m_drop;
    sigc::signal<void(std::vector<std::string>, int)> m_sig_dropped;
    sigc::signal<void(std::string, int)>              m_sig_pasted;
    std::map<core::Style, Glib::RefPtr<Gtk::TextTag>> m_tags;

    // The last scan of what is currently in the buffer. Kept because the click
    // handler needs the same answer the styling needed -- where the checkboxes
    // are -- and rescanning on every click to learn it would be answering a
    // question we already answered this idle.
    core::Scan m_scan;

    // True while show_node() is filling the widgets: GTK fires `changed` on a
    // programmatic set, and writing that back would mark every node modified
    // just for being looked at.
    bool m_loading = false;

    // True while restyle() is applying tags. Folio needed a SECOND guard beyond
    // its loading flag once its classifier started editing text, because the
    // edit path cleared the first one mid-flight. Ticking a checkbox is exactly
    // that kind of edit, so the guard is here from the start rather than after
    // the bug.
    bool m_styling = false;
    bool m_restyle_queued = false;
};

}  // namespace jot
