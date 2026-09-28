#pragma once
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Format -- the format bar's verbs, as text edits (s024).
//
// Scott asked for "a simple format bar to help the user write markdown". The
// source is the truth (s004, s022), so a button is not a style applied to a
// run: it is an edit to the markdown -- wrap a selection in `**`, put `- [ ] `
// in front of three lines, fence a block. This file answers "what edit" with no
// buffer and no display, under selftest; EditorPane only applies the answer as
// one undoable user action. Same division as scan/restyle: a wrong result is a
// selftest failure, never a mystery on screen.
//
// Every verb TOGGLES. Bold on a bold selection unbolds it; Bullet on lines that
// are all bullets makes them plain; Code block inside a fence removes the fence.
// A bar whose buttons only ever add would need a second bar to take away.
//
// Offsets are CODEPOINTS in and out -- what Gtk::TextBuffer::get_iter_at_offset
// wants -- and the conversion to bytes happens here, under test.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

enum class Fmt {
    // inline -- wrap or unwrap the selection (or the word under the cursor)
    Bold, Italic, Strike, Code, Link,
    // line -- the prefix of every line the selection touches
    H1, H2, H3, Plain, Bullet, Numbered, Task, Quote,
    // block
    CodeBlock,
};

// One replacement: codepoints [cp_begin, cp_end) of the body become `text`,
// and the selection afterwards is [sel_begin, sel_end) in the NEW body.
// ok == false means "nothing to do" (e.g. Link across several lines).
struct FmtEdit {
    bool        ok = false;
    int         cp_begin = 0, cp_end = 0;
    std::string text;
    int         sel_begin = 0, sel_end = 0;
};

// sel_begin / sel_end are codepoints, in either order. The cursor with no
// selection is sel_begin == sel_end.
FmtEdit format(const std::string& body, int sel_begin, int sel_end, Fmt f);

// The edit applied to the string -- what the editor's buffer ends up holding.
// For the selftest, and for anyone who wants the answer without a buffer.
std::string apply(const std::string& body, const FmtEdit& e);

}  // namespace jot::core
