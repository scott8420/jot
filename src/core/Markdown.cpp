#include "core/Markdown.hpp"

#include <algorithm>
#include <cctype>

// Markdown.cpp -- block classification, then inline runs inside each block's
// content. Encode and decode are not adjacent here because there is no decode:
// the scan is derived, never stored. The text on disk is the truth and this is
// a view of it computed fresh, which is why nothing here can corrupt a note.

namespace jot::core {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t'; }

// A UTF-8 continuation byte carries no codepoint of its own.
bool is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

// The characters a backslash may legally neutralise. Anything else and the
// backslash is just a backslash -- a notes app full of Windows paths would
// otherwise lose half its punctuation to escape handling.
bool is_escapable(char c) {
    switch (c) {
        case '\\': case '`': case '*': case '_': case '~':
        case '[':  case ']': case '(': case ')': case '#':
        case '!':  case '>': case '-': case '+': case '.':
            return true;
        default:
            return false;
    }
}

// `_` inside a word is snake_case, not emphasis. This app is for a developer's
// notes; `some_var_name` going italic mid-line would be a daily irritation and
// the guard is two comparisons. `*` gets no such guard -- nobody writes
// some*var*name by accident.
bool underscore_ok_to_open(const std::string& t, int i, int content_begin) {
    if (i <= content_begin) return true;
    const char p = t[static_cast<std::size_t>(i - 1)];
    return is_space(p) || p == '(' || p == '[' || p == '"' || p == '\'';
}

int find_char(const std::string& t, char c, int from, int to) {
    for (int i = from; i < to; ++i) {
        if (t[static_cast<std::size_t>(i)] == '\\') { ++i; continue; }
        if (t[static_cast<std::size_t>(i)] == c) return i;
    }
    return -1;
}

int find_pair(const std::string& t, char c, int from, int to) {
    for (int i = from; i + 1 < to; ++i) {
        if (t[static_cast<std::size_t>(i)] == '\\') { ++i; continue; }
        if (t[static_cast<std::size_t>(i)] == c &&
            t[static_cast<std::size_t>(i + 1)] == c)
            return i;
    }
    return -1;
}

void add(std::vector<Span>& out, Style s, int b, int e) {
    if (e > b) out.push_back(Span{s, b, e, 0, 0});
}

// ── inline runs ──────────────────────────────────────────────────────────────
// Left to right, non-nesting, first match wins. Non-nesting is a real
// limitation and a deliberate one: **bold with _italic_ inside** styles the
// bold and leaves the underscores as plain text rather than half-styling them.
// The alternative is a delimiter stack, which is where every markdown parser
// stops being 200 lines, and it buys almost nothing in a scribble pad.
bool tag_open_ok(const std::string& t, int i, int from) {
    if (i <= from) return true;
    const char p = t[static_cast<std::size_t>(i - 1)];
    return is_space(p) || p == '(' || p == '[' || p == '"' || p == '\'';
}

bool tag_body_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ||
           c == '/' || c == '.';
}

void scan_inline(const std::string& t, int from, int to, int line, Scan& sc) {
    std::vector<Span>& out = sc.spans;
    int i = from;
    while (i < to) {
        const char c = t[static_cast<std::size_t>(i)];

        // An escape: dim the backslash, leave the escaped character plain.
        if (c == '\\' && i + 1 < to && is_escapable(t[static_cast<std::size_t>(i + 1)])) {
            add(out, Style::Mark, i, i + 1);
            i += 2;
            continue;
        }

        // Code first, and unconditionally: inside backticks, markdown stops
        // being markdown. Getting this order wrong makes `**` in a code span
        // eat the rest of the line.
        if (c == '`') {
            const int j = find_char(t, '`', i + 1, to);
            if (j >= 0) {
                add(out, Style::Mark, i, i + 1);
                add(out, Style::Code, i + 1, j);
                add(out, Style::Mark, j, j + 1);
                i = j + 1;
                continue;
            }
        }

        // ~~strike~~
        if (c == '~' && i + 1 < to && t[static_cast<std::size_t>(i + 1)] == '~') {
            const int j = find_pair(t, '~', i + 2, to);
            if (j > i + 2) {
                add(out, Style::Mark, i, i + 2);
                add(out, Style::Strike, i + 2, j);
                add(out, Style::Mark, j, j + 2);
                i = j + 2;
                continue;
            }
        }

        // **bold** / __bold__ -- before the single-character italic test, or
        // the first asterisk of ** opens an italic run that never closes.
        if ((c == '*' || c == '_') && i + 1 < to &&
            t[static_cast<std::size_t>(i + 1)] == c &&
            (c == '*' || underscore_ok_to_open(t, i, from))) {
            const int j = find_pair(t, c, i + 2, to);
            if (j > i + 2) {
                add(out, Style::Mark, i, i + 2);
                add(out, Style::Bold, i + 2, j);
                add(out, Style::Mark, j, j + 2);
                i = j + 2;
                continue;
            }
        }

        // *italic* / _italic_
        if ((c == '*' || c == '_') &&
            (c == '*' || underscore_ok_to_open(t, i, from))) {
            const int j = find_char(t, c, i + 1, to);
            if (j > i + 1) {
                add(out, Style::Mark, i, i + 1);
                add(out, Style::Italic, i + 1, j);
                add(out, Style::Mark, j, j + 1);
                i = j + 1;
                continue;
            }
        }

        // [label](target) -- and ![label](target), where the leading bang is
        // just another mark. An image and a link scan identically; what makes
        // one an image is what the editor does with it, later.
        if (c == '[') {
            const int close = find_char(t, ']', i + 1, to);
            if (close > i && close + 1 < to &&
                t[static_cast<std::size_t>(close + 1)] == '(') {
                const int rp = find_char(t, ')', close + 2, to);
                if (rp > close) {
                    // The bang, if there is one, was walked past as plain text
                    // one iteration ago. Claiming it now is a span added out of
                    // order, which costs nothing -- spans overlap freely and GTK
                    // takes them in any order -- and it means an image's marks
                    // dim like every other mark.
                    const bool image = (i > from && t[static_cast<std::size_t>(i - 1)] == '!');
                    const int  start = image ? i - 1 : i;
                    if (image) add(out, Style::Mark, start, i);

                    add(out, Style::Mark, i, i + 1);
                    add(out, Style::Link, i + 1, close);
                    add(out, Style::Mark, close, rp + 1);   // ]( target )

                    Link lk;
                    lk.label  = t.substr(static_cast<std::size_t>(i + 1),
                                         static_cast<std::size_t>(close - i - 1));
                    lk.target = t.substr(static_cast<std::size_t>(close + 2),
                                         static_cast<std::size_t>(rp - close - 2));
                    lk.image  = image;
                    lk.line   = line;
                    lk.begin  = start;
                    lk.end    = rp + 1;
                    sc.links.push_back(std::move(lk));

                    i = rp + 1;
                    continue;
                }
            }
        }

        // #tag. Deliberately narrow: a hash only opens a tag after whitespace
        // or the start of the content, and only before a LETTER. That leaves
        // `C#` alone (no leading space), leaves `#1` alone (a digit, and
        // "#1 priority" is prose), and never collides with a heading -- by then
        // classify() has consumed the hashes and `from` starts past them.
        if (c == '#' && tag_open_ok(t, i, from) && i + 1 < to &&
            std::isalpha(static_cast<unsigned char>(t[static_cast<std::size_t>(i + 1)]))) {
            int j = i + 1;
            while (j < to && tag_body_char(t[static_cast<std::size_t>(j)])) ++j;
            // A trailing `.` or `-` is sentence punctuation, not part of the
            // tag: "#inbox." is a tag at the end of a sentence.
            while (j > i + 1 && (t[static_cast<std::size_t>(j - 1)] == '.' ||
                                 t[static_cast<std::size_t>(j - 1)] == '-')) --j;
            add(out, Style::Tag, i, j);

            Tag tg;
            tg.name  = t.substr(static_cast<std::size_t>(i + 1),
                                static_cast<std::size_t>(j - i - 1));
            tg.line  = line;
            tg.begin = i;
            tg.end   = j;
            sc.tags.push_back(std::move(tg));

            i = j;
            continue;
        }

        ++i;
    }
}

// ── one line ─────────────────────────────────────────────────────────────────
// Returns the byte offset where the line's CONTENT starts (past the block's own
// syntax), so the caller knows where inline scanning may begin.
int classify(const std::string& t, int b, int e, Line& ln, std::vector<Span>& out,
             bool in_fence) {
    int i = b;
    while (i < e && is_space(t[static_cast<std::size_t>(i)])) ++i;

    // Inside a fence nothing is markdown -- with exactly one exception, the
    // line that ENDS the fence. Missing that exception is not a styling bug,
    // it is an unterminated fence: every line to the bottom of the note goes
    // grey and stays grey. (It did, on the first run. The selftest said so in
    // three places at once, which is what a whole-buffer invariant looks like
    // when it breaks.)
    if (in_fence) {
        bool closes = false;
        if (i < e) {
            const char f = t[static_cast<std::size_t>(i)];
            if (f == '`' || f == '~') {
                int n = 0;
                while (i + n < e && t[static_cast<std::size_t>(i + n)] == f) ++n;
                closes = (n >= 3);
            }
        }
        if (closes) {
            ln.block = Block::Fence;
            add(out, Style::Mark, b, e);
            return e;
        }
        ln.block = Block::Code;
        add(out, Style::Code, b, e);
        return e;
    }

    const int indent = i - b;
    if (i >= e) return e;                       // blank line stays Paragraph

    const char c = t[static_cast<std::size_t>(i)];

    // A fence line: ``` or ~~~, three or more. The info string after it ("cpp")
    // is part of the mark, not content.
    if (c == '`' || c == '~') {
        int n = 0;
        while (i + n < e && t[static_cast<std::size_t>(i + n)] == c) ++n;
        if (n >= 3) {
            ln.block = Block::Fence;
            add(out, Style::Mark, b, e);
            return e;
        }
    }

    // A horizontal rule, before the bullet test -- `---` is otherwise a bullet
    // with two more bullets after it.
    if (c == '-' || c == '*' || c == '_') {
        int n = 0, k = i;
        bool only = true;
        while (k < e) {
            const char d = t[static_cast<std::size_t>(k)];
            if (d == c) ++n;
            else if (!is_space(d)) { only = false; break; }
            ++k;
        }
        if (only && n >= 3) {
            ln.block = Block::Rule;
            add(out, Style::Rule, b, e);
            add(out, Style::Mark, b, e);
            return e;
        }
    }

    // # Heading
    if (c == '#') {
        int n = 0;
        while (i + n < e && t[static_cast<std::size_t>(i + n)] == '#') ++n;
        if (n >= 1 && n <= 6 &&
            (i + n >= e || is_space(t[static_cast<std::size_t>(i + n)]))) {
            int content = i + n;
            while (content < e && is_space(t[static_cast<std::size_t>(content)])) ++content;
            ln.block = Block::Heading;
            ln.level = n;
            static const Style H[6] = {Style::H1, Style::H2, Style::H3,
                                       Style::H4, Style::H5, Style::H6};
            add(out, H[n - 1], b, e);            // the whole line scales up
            add(out, Style::Mark, i, content);   // the hashes stay, dimmed
            return content;
        }
    }

    // > quote
    if (c == '>') {
        int content = i + 1;
        if (content < e && is_space(t[static_cast<std::size_t>(content)])) ++content;
        ln.block = Block::Quote;
        add(out, Style::Quote, b, e);
        add(out, Style::Mark, i, content);
        return content;
    }

    // - [ ] task, checked before plain bullet because a task IS a bullet plus
    // a box, and jot's whole premise is that the box is the interesting part.
    if (c == '-' || c == '*' || c == '+') {
        int after = i + 1;
        if (after < e && is_space(t[static_cast<std::size_t>(after)])) {
            int box = after;
            while (box < e && is_space(t[static_cast<std::size_t>(box)])) ++box;
            if (box + 2 < e && t[static_cast<std::size_t>(box)] == '[' &&
                t[static_cast<std::size_t>(box + 2)] == ']') {
                const char mark = t[static_cast<std::size_t>(box + 1)];
                if (mark == ' ' || mark == 'x' || mark == 'X') {
                    int content = box + 3;
                    while (content < e && is_space(t[static_cast<std::size_t>(content)]))
                        ++content;
                    ln.block     = Block::Task;
                    ln.level     = indent;
                    ln.checked   = (mark != ' ');
                    ln.box_begin = box;
                    ln.box_end   = box + 3;
                    add(out, Style::Mark, i, content);
                    if (ln.checked) add(out, Style::TaskDone, content, e);
                    return content;
                }
            }
            // Plain bullet.
            int content = after;
            while (content < e && is_space(t[static_cast<std::size_t>(content)])) ++content;
            ln.block = Block::Bullet;
            ln.level = indent;
            add(out, Style::Mark, i, content);
            return content;
        }
    }

    // 1. numbered / 1) numbered
    if (std::isdigit(static_cast<unsigned char>(c))) {
        int k = i;
        while (k < e && std::isdigit(static_cast<unsigned char>(
                            t[static_cast<std::size_t>(k)]))) ++k;
        if (k < e && (t[static_cast<std::size_t>(k)] == '.' ||
                      t[static_cast<std::size_t>(k)] == ')') &&
            k + 1 < e && is_space(t[static_cast<std::size_t>(k + 1)])) {
            int content = k + 1;
            while (content < e && is_space(t[static_cast<std::size_t>(content)])) ++content;
            ln.block = Block::Numbered;
            ln.level = indent;
            add(out, Style::Mark, i, content);
            return content;
        }
    }

    return b;   // Paragraph: content is the whole line
}

}  // namespace

const char* tag_name(Style s) {
    switch (s) {
        case Style::Mark:     return "md-mark";
        case Style::H1:       return "md-h1";
        case Style::H2:       return "md-h2";
        case Style::H3:       return "md-h3";
        case Style::H4:       return "md-h4";
        case Style::H5:       return "md-h5";
        case Style::H6:       return "md-h6";
        case Style::Bold:     return "md-bold";
        case Style::Italic:   return "md-italic";
        case Style::Code:     return "md-code";
        case Style::Strike:   return "md-strike";
        case Style::Link:     return "md-link";
        case Style::Tag:      return "md-tag";
        case Style::Quote:    return "md-quote";
        case Style::TaskDone: return "md-task-done";
        case Style::Rule:     return "md-rule";
    }
    return "md-unknown";
}

const std::vector<Style>& all_styles() {
    // Creation order IS priority order in GTK, so Mark is deliberately last:
    // a dimmed asterisk inside a bold run must stay dimmed, or the syntax
    // disappears into the thing it marks and the "marks stay visible" decision
    // quietly stops being true.
    static const std::vector<Style> v{
        Style::Rule,  Style::Quote, Style::Code,   Style::Link,
        Style::Tag,
        Style::H1,    Style::H2,    Style::H3,     Style::H4,
        Style::H5,    Style::H6,    Style::Bold,   Style::Italic,
        Style::Strike, Style::TaskDone,
        Style::Mark,
    };
    return v;
}

std::string link_node_id(const std::string& target) {
    const std::string prefix = kJotScheme;
    if (target.size() <= prefix.size()) return {};
    if (target.compare(0, prefix.size(), prefix) != 0) return {};
    std::string id = target.substr(prefix.size());
    // Tolerate `jot://id` and surrounding space: a link is typed by hand, and
    // refusing one over a slash the user could not see would be a mystery.
    while (!id.empty() && id.front() == '/') id.erase(id.begin());
    while (!id.empty() && is_space(id.front())) id.erase(id.begin());
    while (!id.empty() && is_space(id.back())) id.pop_back();
    return id;
}

int cp_len(const std::string& text, int begin, int end) {
    begin = std::max(0, begin);
    end   = std::min(end, static_cast<int>(text.size()));
    int n = 0;
    for (int i = begin; i < end; ++i)
        if (!is_cont(static_cast<unsigned char>(text[static_cast<std::size_t>(i)]))) ++n;
    return n;
}

Scan scan(const std::string& text) {
    Scan sc;
    const int n = static_cast<int>(text.size());

    bool in_fence = false;
    int  b = 0;
    while (true) {
        int e = b;
        while (e < n && text[static_cast<std::size_t>(e)] != '\n') ++e;

        Line ln;
        ln.begin = b;
        ln.end   = e;
        const int line = static_cast<int>(sc.lines.size());
        const int content = classify(text, b, e, ln, sc.spans, in_fence);
        if (ln.block == Block::Fence) in_fence = !in_fence;
        if (ln.block != Block::Code && ln.block != Block::Fence &&
            ln.block != Block::Rule)
            scan_inline(text, content, e, line, sc);
        sc.lines.push_back(ln);

        if (e >= n) break;
        b = e + 1;
    }

    // ── bytes → codepoints, in ONE pass over the text ────────────────────────
    // Every offset the editor will ever ask for is already in hand, so they get
    // converted together rather than one at a time against a buffer that would
    // have to be re-walked for each. Everything below is offset bookkeeping; the
    // markdown is finished above.
    // A flat byte→codepoint table, filled in one walk. The first draft
    // collected every offset, sorted them and binary-searched each lookup;
    // it measured 81ms on the budget body, nearly all of it in the sort and
    // the per-span lower_bound. This costs one int per byte of note -- a few
    // kilobytes for a real note -- and makes every lookup an array index.
    // Offsets are ASKED FOR in scattered order, so anything clever about
    // ordering them was always going to lose to just having them all.
    std::vector<int> cp_at(static_cast<std::size_t>(n) + 1, 0);
    {
        int cp = 0;
        for (int byte = 0; byte < n; ++byte) {
            cp_at[static_cast<std::size_t>(byte)] = cp;
            if (!is_cont(static_cast<unsigned char>(text[static_cast<std::size_t>(byte)])))
                ++cp;
        }
        // Each entry is the codepoint count BEFORE that byte, so the one-past-
        // the-end entry is the total and an end-offset lands on it correctly.
        // Continuation bytes get an entry too and it is meaningless -- every
        // offset the scanner produces is a character boundary, by construction.
        cp_at[static_cast<std::size_t>(n)] = cp;
    }
    const auto cp_of = [&](int byte) {
        return cp_at[static_cast<std::size_t>(std::clamp(byte, 0, n))];
    };

    for (auto& l : sc.lines) {
        l.cp_begin = cp_of(l.begin);
        l.cp_end   = cp_of(l.end);
        if (l.box_begin >= 0) {
            l.cp_box_begin = cp_of(l.box_begin);
            l.cp_box_end   = cp_of(l.box_end);
        }
    }
    for (auto& s : sc.spans) {
        s.cp_begin = cp_of(s.begin);
        s.cp_end   = cp_of(s.end);
    }
    for (auto& l : sc.links) {
        l.cp_begin = cp_of(l.begin);
        l.cp_end   = cp_of(l.end);
    }
    for (auto& g : sc.tags) {
        g.cp_begin = cp_of(g.begin);
        g.cp_end   = cp_of(g.end);
    }

    return sc;
}

// ─────────────────────────────────────────────────────────────────────────────
// first_prose_line -- the WHY, in one line.
//
// Moved out of TodayPane.cpp in s009 when the desktop projection became a
// second consumer. Byte walk, codepoint-aware truncation: the cut counts
// characters so it cannot land inside one, which a byte-length cut does the
// first time a note opens with an em dash.
// ─────────────────────────────────────────────────────────────────────────────
std::string first_prose_line(const std::string& body, int max_chars) {
    std::size_t i = 0;
    while (i < body.size()) {
        std::size_t e = body.find('\n', i);
        if (e == std::string::npos) e = body.size();
        std::string line = body.substr(i, e - i);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();

        const bool blank   = line.find_first_not_of(" \t") == std::string::npos;
        const bool heading = !line.empty() && line.front() == '#';
        if (!blank && !heading) {
            if (max_chars > 0) {
                // Walk codepoints, not bytes. Continuation bytes are 0b10xxxxxx.
                int         cps = 0;
                std::size_t cut = 0;
                for (std::size_t b = 0; b < line.size(); ++b) {
                    if ((static_cast<unsigned char>(line[b]) & 0xC0) == 0x80) continue;
                    if (cps == max_chars - 2) cut = b;
                    ++cps;
                }
                if (cps > max_chars && cut > 0) line = line.substr(0, cut) + "\u2026";
            }
            return line;
        }
        i = e + 1;
    }
    return {};
}

}  // namespace jot::core
