#pragma once
#include "core/Markdown.hpp"

#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Render -- the READING view of a note (s021).
//
// Scott, s021: "What we ultimately want is an obsidian type work flow", with
// OmniFocus's getting-things-done at the centre. Obsidian has three views of
// one note: Source (every mark on screen -- jot's editor since s004), Reading
// (the marks gone, the note as it reads), and Live Preview (formatted while you
// type, a mark appearing only where the cursor is). This file is Reading, built
// so that Live Preview can stand on it: the render is the SOURCE MINUS ITS
// MARKS, and it carries a codepoint map back to the source.
//
// Not a second markdown parser. core::scan already decides which bytes are
// syntax (Style::Mark); the render drops them, and swaps in the few glyphs
// that stand for a mark rather than just losing it:
//
//   `- `         ->  "• "            a bullet
//   `- [ ] `     ->  "☐ " / "☑ "     a task (clickable in the view)
//   `1. `        ->  kept            the number IS content
//   `---`        ->  U+FFFC          an anchor: the view puts a separator there
//   `![l](t)`    ->  U+FFFC          an anchor: the view puts the picture there
//   a ``` block  ->  U+FFFC          an anchor: the view draws a code bubble
//                                    with a copy button (s021b)
//
// U+FFFC is what GtkTextBuffer itself uses for a child anchor, and it is ONE
// codepoint, so the view swaps each placeholder for a real anchor without
// moving a single offset. The map stays exact.
//
// GTK-free, like everything under core/. Selftested.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

inline constexpr const char* kAnchorChar = "\xEF\xBF\xBC";   // U+FFFC

struct RenderAnchor {
    enum class Kind { Image, Rule, Code };
    Kind        kind = Kind::Rule;
    int         cp = 0;          // the placeholder's codepoint in Rendered::text
    std::string target;          // Image: the link target, as written
    std::string label;           // Image: the alt text, shown if it cannot load
    std::string code;            // Code: the lines between the fences, as written
    std::string lang;            // Code: the info string after the opening fence ("cpp")
};

struct RenderLink {
    int         cp_begin = 0, cp_end = 0;   // the visible label
    std::string target;
};

struct RenderBox {
    int  cp = 0;                 // the glyph
    int  line = 0;               // the SOURCE line the task is on
    bool checked = false;
};

struct Rendered {
    std::string               text;      // what the reading view shows
    std::vector<Span>         spans;     // cp_* index into `text`; never Style::Mark
    std::vector<RenderAnchor> anchors;
    std::vector<RenderLink>   links;     // not images: those are anchors
    std::vector<RenderBox>    boxes;
    // src_cp[i] = the source codepoint rendered codepoint i came from, for
    // every i in [0, cp length of text]; the last entry is the source's end.
    // Non-decreasing. A glyph maps to the first byte of the mark it replaced.
    std::vector<int>          src_cp;
};

Rendered render(const std::string& body);

// Rendered codepoint -> source codepoint (clamped).
int source_cp(const Rendered& r, int rendered_cp);

// Source codepoint -> the first rendered codepoint at or after it (clamped).
// A position inside a hidden mark lands on what follows the mark.
int rendered_cp(const Rendered& r, int source_cp);

// ── Live Preview (s022, s023) ───────────────────────────────────────────────
// Obsidian's third view: the note you TYPE into, formatted, a mark showing
// only where the cursor is. NOT built on the render above, and on purpose:
// Live Preview is an editor, and an editor over a second buffer is a second
// coordinate space every keystroke would have to be mapped through. It is the
// SOURCE buffer, byte for byte, with some Mark runs tagged invisible, and
// (s023) a few things DRAWN over it -- a bullet, a box, a rule, a picture, a
// code block's Copy button -- in the text view's own drawing layer. Nothing is
// ever inserted into the buffer. This function is the rule; EditorPane only
// tags and draws what it answers.
//
// Off the revealed lines:
//   - heading hashes, a quote's `>`, emphasis, `code` ticks, ~~, escapes,
//     a link's `[` and `](target)`            -> hidden
//   - `- ` of a bullet                        -> hidden, a Bullet drawn
//   - `- [ ] ` of a task                      -> hidden, a Box drawn (clickable)
//   - `1. `                                   -> kept: the number IS content
//   - `---`                                   -> hidden, a Rule drawn
//   - a line that is ONLY `![l](t)`           -> an Image deco; NOT in
//     `hidden` -- the editor hides it only if it can load the picture, so a
//     missing file keeps its marks, which then say what is missing
//   - a fenced block                          -> both fence lines hidden
//     (newline too, so they take no room), a Code deco for the Copy button.
//     A block reveals WHOLE when the cursor is anywhere in it.
// Lines [reveal_first, reveal_last] are the cursor's (or every line a
// selection touches). All ranges are codepoints into the scanned text,
// sorted and non-overlapping; `hidden` may run one past the last codepoint
// (a trailing newline that is not there) and the editor clamps.
struct CpRange { int begin = 0, end = 0; };

struct LiveDeco {
    enum class Kind { Bullet, Box, Rule, Image, Code };
    Kind        kind = Kind::Bullet;
    int         line = 0;        // Code: the first CODE line (after the fence)
    int         cp = 0;          // Bullet/Box: where the content starts; Image: the link
    int         cp_end = 0;      // Image: end of the link
    bool        checked = false; // Box
    std::string target, label;   // Image
    std::string code, lang;      // Code: the text between the fences; the info string
};

struct LiveView {
    std::vector<CpRange>  hidden;
    std::vector<LiveDeco> decos;
    std::vector<int>      hang_lines;   // bullet / task lines whose mark is hidden:
                                        // the editor indents them to make room for the glyph
};

LiveView live_view(const Scan& sc, const std::string& text, int reveal_first, int reveal_last);

}  // namespace jot::core
