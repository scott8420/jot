#pragma once
#include <string>
#include <vector>

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

// s027: a further replacement, made AFTER the main one -- a list's numbers
// put right. Codepoints into the body as the main edit left it.
struct FmtPatch {
    int         cp_begin = 0, cp_end = 0;
    std::string text;
};

// One replacement: codepoints [cp_begin, cp_end) of the body become `text`,
// and the selection afterwards is [sel_begin, sel_end) in the NEW body (after
// `then` too). ok == false means "nothing to do" (e.g. Link across several
// lines). `then` (s027): patches over the body after the main edit, sorted,
// never overlapping; applied last-first so each one's offsets hold. The editor
// makes main + then ONE undoable action.
struct FmtEdit {
    bool        ok = false;
    int         cp_begin = 0, cp_end = 0;
    std::string text;
    int         sel_begin = 0, sel_end = 0;
    std::vector<FmtPatch> then;
};

// sel_begin / sel_end are codepoints, in either order. The cursor with no
// selection is sel_begin == sel_end.
FmtEdit format(const std::string& body, int sel_begin, int sel_end, Fmt f);

// ── typing in a list (s025) ─────────────────────────────────────────────────
// Enter at the cursor. On a list, task, numbered or quote line: the next line
// starts with the same mark (a task unticked, a number plus one), the words
// after the cursor going with it. On an EMPTY item: a nested one steps out a
// level, a top-level one loses its mark -- Enter twice ends a list. ok ==
// false means "not ours": a plain line, a selection, a code block, the cursor
// inside the mark -- let the text view insert its own newline.
FmtEdit enter(const std::string& body, int sel_begin, int sel_end);

// Tab (outdent = false) or Shift+Tab on the list lines a selection touches.
// In: the item nests under the one above, its mark lining up with that item's
// words. Out: back to its parent's level. ok == false when no touched line is
// a list line -- a Tab there is the text view's. ok with an unchanged text
// means the key was ours and there was nothing to do (a top-level Shift+Tab).
FmtEdit indent(const std::string& body, int sel_begin, int sel_end, bool outdent);

// ── numbered lists renumber (s027) ─────────────────────────────────────────
// Scott, after s026: Obsidian-style. Each run of numbered items at one level
// counts on from its first item's number; a nested run restarts at 1 under its
// parent. enter / indent / the list verbs of format() renumber the list they
// touched as part of their own edit. This one is for the edits the text view
// makes itself -- a deleted item, joined lines: the list around the cursor's
// line, put right. A list keeps the number it STARTS at (`5. 6. 7.`); an item
// a verb just moved that now starts a run starts it at 1.
// ok == false: nothing to renumber. The main edit is empty; it is all `then`,
// and the selection [sel_begin, sel_end] is carried through it.
FmtEdit renumber(const std::string& body, int sel_begin, int sel_end);

// Backspace with the cursor at the first character of a bullet's or task's
// words (Live Preview draws the mark there, so the `- ` is not on screen to
// delete a space of): the mark goes, the indent and the words stay. ok == false
// anywhere else -- the text view's own Backspace.
FmtEdit backspace(const std::string& body, int sel_begin, int sel_end);

// The edit applied to the string -- what the editor's buffer ends up holding.
// For the selftest, and for anyone who wants the answer without a buffer.
std::string apply(const std::string& body, const FmtEdit& e);

}  // namespace jot::core
