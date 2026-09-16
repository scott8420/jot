#pragma once
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Markdown -- the scanner behind the body pane's styling.
//
// GTK-free ON PURPOSE, and it is the reason this file is under core/: the whole
// question "what is this line, and which bytes of it are syntax" is answerable
// with no display, so it gets answered in the selftest where a wrong answer is
// visible. The editor's job shrinks to "apply a tag where the scan says to",
// which is the part that genuinely needs a buffer.
//
// D5 (ANSWERED s004): Folio has no markdown editor to lift -- its editor is
// WYSIWYG over a GtkTextBuffer serialized to HTML, and markdown appears only at
// its import/export boundary with no inline parsing at all. What DID lift is the
// shape of its screenplay auto-sense: classify on idle, per line, behind a
// re-entry guard, never fighting the user. That shape is in EditorPane; this
// file is the part Folio never had.
//
// The visible-marks decision (Scott, s004): `## Heading` renders large AND the
// hashes stay on screen, dimmed. The buffer is always byte-identical to the file
// on disk. Nothing is hidden, so nothing has to be un-hidden when the cursor
// arrives, and TextMap stays out of the editor entirely. The Obsidian-style
// hiding variant is this plus a reveal rule, and is deliberately not built.
//
// OFFSETS COME IN PAIRS. Everything carries `begin/end` in BYTES (what a
// std::string scanner naturally produces) and `cp_begin/cp_end` in CODEPOINTS
// (what Gtk::TextBuffer::get_iter_at_offset actually wants). The conversion is
// done here, in one pass, under test -- rather than in the editor where a UTF-8
// bug would present as "styling drifts right after you type an em dash".
// ─────────────────────────────────────────────────────────────────────────────

namespace jot::core {

// What a whole line IS. One per line, mutually exclusive.
enum class Block {
    Paragraph,   // anything unclaimed
    Heading,     // # .. ######            level = 1..6
    Bullet,      // - * +                  level = indent in spaces
    Numbered,    // 1. 2)                  level = indent in spaces
    Task,        // - [ ] / - [x]          level = indent in spaces
    Quote,       // >
    Fence,       // the ``` line itself
    Code,        // a line inside a fence
    Rule,        // --- *** ___
};

// What a RUN of bytes is. Spans overlap freely -- a heading emits one H1 across
// its text and one Mark across its hashes, and GTK is happy to carry both.
enum class Style {
    Mark,        // syntax punctuation: the hashes, the asterisks, the link parens
    H1, H2, H3, H4, H5, H6,
    Bold,
    Italic,
    Code,        // `inline` and fenced bodies alike
    Strike,
    Link,        // the visible label of [label](target)
    Tag,         // #tag
    Quote,
    TaskDone,    // the text of a checked task line
    Rule,
};

// The tag name jot registers with the buffer for a style: "md-h1", "md-mark".
// One function so the tag table and the scan cannot drift apart -- the
// SHORTCUT-REGISTRY lesson (one list, two consumers) applied to styling.
const char* tag_name(Style s);

// Every style, in the order tags must be CREATED. GtkTextTag priority is
// creation order (later wins), and Mark is last so a dimmed asterisk stays
// dimmed even inside a bold run.
const std::vector<Style>& all_styles();

struct Span {
    Style style;
    int   begin = 0, end = 0;          // bytes into the scanned text
    int   cp_begin = 0, cp_end = 0;    // codepoints -- what the buffer wants
};

struct Line {
    Block block = Block::Paragraph;
    int   level = 0;
    int   begin = 0, end = 0;          // bytes, newline NOT included
    int   cp_begin = 0, cp_end = 0;

    // A task's checkbox, for hit-testing a click. -1 when the line is not a
    // task. This is why the scan is a model artefact and not a styling detail:
    // "click the box to tick it" needs the same answer the styling needed.
    bool  checked = false;
    int   box_begin = -1, box_end = -1;
    int   cp_box_begin = -1, cp_box_end = -1;
};

// ── the things a note POINTS AT ─────────────────────────────────────────────
// s004 styled links and stopped there: it emitted a Span over the label and a
// Span over `](target)` and threw the target string away, because styling never
// needed to know where a link went. The drawer does. Extracting it here rather
// than re-parsing in the drawer keeps ONE answer to "what does this note link
// to" -- the alternative is a second, simpler, wrong markdown parser living in
// a UI file, which is how the two drift.
struct Link {
    std::string label;             // the text between [ and ]
    std::string target;            // the text between ( and )
    bool        image = false;     // ![label](target) -- the leading bang
    int         line  = 0;         // which buffer line it sits on
    int         begin = 0, end = 0;          // bytes, the WHOLE link incl. any !
    int         cp_begin = 0, cp_end = 0;
};

// A `#tag`. READ-ONLY for now and that matters: D4 (where GTD state lives) is
// open, so the drawer SHOWS what the body says and offers no way to edit it.
// Scanning for them commits nothing -- it is a derived view of bytes that are
// already there -- and it is the cheapest way to look at inline tags before
// deciding whether they are the answer.
//
// `#` opens a tag only after whitespace/start and only before a LETTER, so
// `C#`, `#1 priority` and a `# Heading`'s hash are all left alone.
struct Tag {
    std::string name;              // WITHOUT the hash
    int         line  = 0;
    int         begin = 0, end = 0;          // bytes, hash included
    int         cp_begin = 0, cp_end = 0;
};

struct Scan {
    std::vector<Line> lines;   // index == buffer line number, always
    std::vector<Span> spans;
    std::vector<Link> links;
    std::vector<Tag>  tags;
};

// jot's own link scheme: `[label](jot:<node id>)`. Returns the id, or EMPTY for
// anything else (http, a relative path, an attachment). One function so the
// prefix is written down once -- the index, the drawer and the copy-link action
// all resolve through it and cannot disagree about what a jot link looks like.
inline constexpr const char* kJotScheme = "jot:";
std::string link_node_id(const std::string& target);

// Scan a whole body. Whole-buffer rather than per-line BY DESIGN: a fence opened
// on line 3 restyles everything under it, so there is no such thing as a correct
// line-local markdown restyle. The scan is a pure string walk with no
// allocation per character, and the selftest holds it to a budget on a body far
// larger than a note will ever be.
Scan scan(const std::string& text);

// Codepoint length of a byte range -- exposed because the editor needs it when
// it edits a line and wants the cursor back where it was.
int cp_len(const std::string& text, int begin, int end);

// THE WHY, in one line: the first non-blank, non-heading line of a body.
//
// Headings are skipped because a project note very often opens with its own
// title as an H1, and repeating the group header under the group header says
// nothing. Truncated with an ellipsis at `max_chars` CODEPOINTS -- not bytes,
// or the cut lands inside a character.
//
// It lives here rather than in a view because s009 gave it a SECOND consumer:
// Today draws it under a group header, and the desktop projection puts it in a
// VTODO description. Two copies of "what does this note say in one line" would
// drift the moment one of them learned about, say, block quotes.
std::string first_prose_line(const std::string& body, int max_chars = 90);

}  // namespace jot::core
