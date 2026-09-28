#include "core/Format.hpp"
#include "core/Markdown.hpp"

#include <algorithm>
#include <vector>

// See Format.hpp. Everything here works on a u32string so an offset is a
// codepoint and "one character back" is one index back; the body is decoded
// once on the way in and the replacement encoded once on the way out.
namespace jot::core {

namespace {

using U = std::u32string;

U decode(const std::string& s) {
    U out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        int n = 1;
        if (c >= 0xF0)      { cp = c & 0x07; n = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
        for (int k = 1; k < n && i + static_cast<std::size_t>(k) < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
        out.push_back(cp);
        i += static_cast<std::size_t>(n);
    }
    return out;
}

std::string encode(const U& u) {
    std::string out;
    out.reserve(u.size());
    for (char32_t c : u) {
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

U u(const char* s) { return decode(s); }

bool is_space(char32_t c) { return c == U' ' || c == U'\t'; }
bool is_digit(char32_t c) { return c >= U'0' && c <= U'9'; }
bool is_word(char32_t c) {
    if (c == U'_') return true;
    if (c < 0x80) return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || is_digit(c);
    // Letters outside ASCII are word characters; the punctuation a writer
    // actually puts next to a word is not.
    return !(c == 0x2014 || c == 0x2013 || c == 0x2026 || c == 0x00A0 ||
             (c >= 0x2018 && c <= 0x201F));
}

// ── lines ────────────────────────────────────────────────────────────────────
struct Lines {
    std::vector<int> start;   // start[i] = first cp of line i
    int size = 0;             // cp length of the whole text
    int end(int i) const {    // one past the line's last cp, newline excluded
        return i + 1 < static_cast<int>(start.size()) ? start[static_cast<std::size_t>(i) + 1] - 1 : size;
    }
    int of(int p) const {
        auto it = std::upper_bound(start.begin(), start.end(), p);
        return static_cast<int>(it - start.begin()) - 1;
    }
    int count() const { return static_cast<int>(start.size()); }
};

Lines lines_of(const U& t) {
    Lines L;
    L.size = static_cast<int>(t.size());
    L.start.push_back(0);
    for (int i = 0; i < L.size; ++i)
        if (t[static_cast<std::size_t>(i)] == U'\n') L.start.push_back(i + 1);
    return L;
}

// The lines a selection touches. A selection that ENDS at the start of a line
// (a triple-click, a drag to the left edge) does not touch that line.
void touched(const Lines& L, int a, int b, int& la, int& lb) {
    la = L.of(a);
    lb = L.of(b);
    if (b > a && lb > la && b == L.start[static_cast<std::size_t>(lb)]) --lb;
}

// ── edits over the original, and where a position lands after them ─────────
struct Ed { int pos, del; U ins; };

U rebuild(const U& t, int r0, int r1, std::vector<Ed> eds) {
    std::stable_sort(eds.begin(), eds.end(), [](const Ed& x, const Ed& y) { return x.pos < y.pos; });
    U out;
    int p = r0;
    for (const auto& e : eds) {
        out.append(t, static_cast<std::size_t>(p), static_cast<std::size_t>(e.pos - p));
        out += e.ins;
        p = e.pos + e.del;
    }
    out.append(t, static_cast<std::size_t>(p), static_cast<std::size_t>(r1 - p));
    return out;
}

// `inclusive`: an insertion AT p counts (p is where content starts, and the
// opening mark goes in front of it). Otherwise only edits strictly before p.
int shifted(int p, const std::vector<Ed>& eds, bool inclusive) {
    int d = 0;
    for (const auto& e : eds)
        if (e.pos < p || (inclusive && e.pos == p))
            d += static_cast<int>(e.ins.size()) - std::min(e.del, std::max(0, p - e.pos));
    return p + d;
}

// ── inline marks ─────────────────────────────────────────────────────────────
U mark_of(Fmt f) {
    switch (f) {
        case Fmt::Bold:   return u("**");
        case Fmt::Italic: return u("*");
        case Fmt::Strike: return u("~~");
        default:          return u("`");
    }
}

int stars_before(const U& t, int p, int floor) {
    int k = 0;
    while (p - k - 1 >= floor && t[static_cast<std::size_t>(p - k - 1)] == U'*') ++k;
    return k;
}
int stars_after(const U& t, int p, int ceil) {
    int k = 0;
    while (p + k < ceil && t[static_cast<std::size_t>(p + k)] == U'*') ++k;
    return k;
}

// Bold and italic share a character, so "is this wrapped" is a count, not a
// string compare: `**x**` is bold and not italic, `***x***` is both.
bool star_fits(Fmt f, int k) {
    return f == Fmt::Bold ? (k == 2 || k == 3) : (k == 1 || k == 3);
}

bool match(const U& t, int p, const U& m, int lo, int hi) {
    const int n = static_cast<int>(m.size());
    return p >= lo && p + n <= hi && t.compare(static_cast<std::size_t>(p), static_cast<std::size_t>(n), m) == 0;
}

// Marks just OUTSIDE [s,e), within the line [lo,hi).
bool wrapped_outside(const U& t, int s, int e, Fmt f, int lo, int hi) {
    if (f == Fmt::Bold || f == Fmt::Italic)
        return star_fits(f, stars_before(t, s, lo)) && star_fits(f, stars_after(t, e, hi));
    const U m = mark_of(f);
    const int n = static_cast<int>(m.size());
    return match(t, s - n, m, lo, hi) && match(t, e, m, lo, hi);
}

FmtEdit finish(const U& t, int r0, int r1, const std::vector<Ed>& eds, int sel_a, int sel_b) {
    FmtEdit out;
    out.ok        = true;
    out.cp_begin  = r0;
    out.cp_end    = r1;
    out.text      = encode(rebuild(t, r0, r1, eds));
    out.sel_begin = sel_a;
    out.sel_end   = sel_b;
    return out;
}

FmtEdit inline_mark(const U& t, int a, int b, Fmt f) {
    const Lines L = lines_of(t);
    const U m = mark_of(f);
    const int n = static_cast<int>(m.size());

    // ── the cursor alone: the word under it, or an empty pair ──────────────
    if (a == b) {
        const int ln = L.of(a), lo = L.start[static_cast<std::size_t>(ln)], hi = L.end(ln);
        int wa = a, wb = a;
        while (wa > lo && is_word(t[static_cast<std::size_t>(wa - 1)])) --wa;
        while (wb < hi && is_word(t[static_cast<std::size_t>(wb)])) ++wb;
        if (wa == wb) {
            // Nothing to wrap. Between an empty pair -> take the pair away
            // (the second press of Ctrl+B undoes the first); else insert one.
            if (match(t, a - n, m, lo, hi) && match(t, a, m, lo, hi) &&
                (f != Fmt::Italic || (stars_before(t, a, lo) == 1 && stars_after(t, a, hi) == 1)))
                return {true, a - n, a + n, "", a - n, a - n};
            return {true, a, a, encode(m + m), a + n, a + n};
        }
        std::vector<Ed> eds;
        if (wrapped_outside(t, wa, wb, f, lo, hi)) {
            eds.push_back({wa - n, n, {}});
            eds.push_back({wb, n, {}});
        } else {
            eds.push_back({wa, 0, m});
            eds.push_back({wb, 0, m});
        }
        const int r0 = std::max(lo, wa - n), r1 = std::min(hi, wb + n);
        const int c = shifted(a, eds, a != wb);   // at the word's end, stay inside the close mark
        return finish(t, r0, r1, eds, c, c);
    }

    // ── a selection: one segment per line it touches ───────────────────────
    int la = 0, lb = 0;
    touched(L, a, b, la, lb);
    struct Seg { int s, e, lo, hi; bool out; };
    std::vector<Seg> segs;
    for (int i = la; i <= lb; ++i) {
        const int lo = L.start[static_cast<std::size_t>(i)], hi = L.end(i);
        int s = std::max(a, lo), e = std::min(b, hi);
        // Spaces stay OUTSIDE the marks: `** x**` is not bold.
        while (s < e && is_space(t[static_cast<std::size_t>(s)])) ++s;
        while (e > s && is_space(t[static_cast<std::size_t>(e - 1)])) --e;
        if (s == e) continue;
        // Marks the user selected along with the words are not the words:
        // shrink past them, so `{**one**}`, `**{one**}` and `**{one}**` are all
        // the same question -- is `one` bold? One check (outside) then answers
        // all three, and the words are what ends up selected.
        const char32_t mc = m[0];
        int ss = s, ee = e;
        while (ss < ee && t[static_cast<std::size_t>(ss)] == mc) ++ss;
        while (ee > ss && t[static_cast<std::size_t>(ee - 1)] == mc) --ee;
        if (ss < ee) { s = ss; e = ee; }
        segs.push_back({s, e, lo, hi, wrapped_outside(t, s, e, f, lo, hi)});
    }
    if (segs.empty()) return {};

    const bool unwrap = std::all_of(segs.begin(), segs.end(), [](const Seg& g) { return g.out; });
    std::vector<Ed> eds;
    int ca = 0, cb = 0;   // content bounds of the first / last segment, in the old text
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const auto& g = segs[i];
        if (unwrap) {
            eds.push_back({g.s - n, n, {}});
            eds.push_back({g.e, n, {}});
        } else if (!g.out) {
            eds.push_back({g.s, 0, m});
            eds.push_back({g.e, 0, m});
        }
        if (i == 0) ca = g.s;
        cb = g.e;
    }
    const int r0 = L.start[static_cast<std::size_t>(la)], r1 = L.end(lb);
    return finish(t, r0, r1, eds, shifted(ca, eds, true), shifted(cb, eds, false));
}

// ── a link ───────────────────────────────────────────────────────────────────
bool urlish(const U& s) {
    for (const char* p : {"http://", "https://", "file://", "mailto:", "jot:"}) {
        const U q = u(p);
        if (s.size() > q.size() && s.compare(0, q.size(), q) == 0) return true;
    }
    return false;
}

FmtEdit link(const U& t, int a, int b) {
    const Lines L = lines_of(t);
    if (L.of(a) != L.of(b)) return {};   // a link is one line
    if (a == b) return {true, a, a, "[]()", a + 1, a + 1};
    const U sel = t.substr(static_cast<std::size_t>(a), static_cast<std::size_t>(b - a));
    const int n = static_cast<int>(sel.size());
    // A selected address becomes the target, and the label is asked for;
    // selected words become the label, and the address is asked for.
    if (urlish(sel)) return {true, a, b, encode(u("[](") + sel + u(")")), a + 1, a + 1};
    return {true, a, b, encode(u("[") + sel + u("]()")), a + n + 3, a + n + 3};
}

// ── line prefixes ────────────────────────────────────────────────────────────
enum class Kind { Para, Heading, Bullet, Numbered, Task };

struct Pre {
    bool quote = false;
    int  q0 = 0, qe = 0;     // the `> ` marker: [q0, qe); qe = 0 when none
    int  ind_end = 0;        // end of the indent after any quote
    Kind kind = Kind::Para;
    int  level = 0;
    bool checked = false;
    int  body = 0;           // first cp of the text proper
    bool blank = false;
    char32_t bullet = U'-';  // the list mark as written: - * +
    long     num = 0;        // a numbered item's number
    char32_t delim = U'.';   // and its . or )
};

Pre parse(const U& s) {
    Pre p;
    const int n = static_cast<int>(s.size());
    int i = 0;
    while (i < n && is_space(s[static_cast<std::size_t>(i)])) ++i;
    if (i < n && s[static_cast<std::size_t>(i)] == U'>') {
        p.quote = true;
        p.q0 = i;
        ++i;
        if (i < n && s[static_cast<std::size_t>(i)] == U' ') ++i;
        p.qe = i;
        while (i < n && is_space(s[static_cast<std::size_t>(i)])) ++i;
    }
    p.ind_end = i;
    auto at = [&](int k) -> char32_t { return k < n ? s[static_cast<std::size_t>(k)] : U'\0'; };
    auto sp_or_end = [&](int k) { return k >= n || at(k) == U' '; };
    const char32_t c = at(i);

    if (c == U'#') {
        int k = i;
        while (at(k) == U'#') ++k;
        if (k - i <= 6 && sp_or_end(k)) {
            p.kind = Kind::Heading;
            p.level = k - i;
            p.body = std::min(n, k + 1);
        }
    } else if ((c == U'-' || c == U'*' || c == U'+') && (at(i + 1) == U' ' || i + 1 >= n)) {
        p.bullet = c;
        const char32_t st = at(i + 3);
        if (at(i + 2) == U'[' && (st == U' ' || st == U'x' || st == U'X') && at(i + 4) == U']' &&
            sp_or_end(i + 5)) {
            p.kind = Kind::Task;
            p.checked = st != U' ';
            p.body = std::min(n, i + 6);
        } else {
            p.kind = Kind::Bullet;
            p.body = std::min(n, i + 2);
        }
    } else if (is_digit(c)) {
        int k = i;
        while (is_digit(at(k))) ++k;
        if (k - i <= 9 && (at(k) == U'.' || at(k) == U')') && sp_or_end(k + 1)) {
            p.kind = Kind::Numbered;
            p.delim = at(k);
            for (int d = i; d < k; ++d) p.num = p.num * 10 + (s[static_cast<std::size_t>(d)] - U'0');
            p.body = std::min(n, k + 2);
        }
    }
    if (p.kind == Kind::Para) p.body = p.ind_end;
    p.blank = p.body >= n && p.kind == Kind::Para && !p.quote;
    return p;
}

FmtEdit line_prefix(const U& t, int a, int b, Fmt f) {
    const Lines L = lines_of(t);
    int la = 0, lb = 0;
    touched(L, a, b, la, lb);

    std::vector<U>   text;
    std::vector<Pre> pre;
    for (int i = la; i <= lb; ++i) {
        const int s = L.start[static_cast<std::size_t>(i)];
        text.push_back(t.substr(static_cast<std::size_t>(s), static_cast<std::size_t>(L.end(i) - s)));
        pre.push_back(parse(text.back()));
    }
    // Blank lines inside a selection are left alone -- unless the selection
    // IS a blank line, which is how you start a list from nothing.
    const bool all_blank = std::all_of(pre.begin(), pre.end(), [](const Pre& p) { return p.blank; });
    auto target = [&](std::size_t i) { return all_blank || !pre[i].blank; };

    bool all = true;   // every target line already has what the verb gives
    for (std::size_t i = 0; i < pre.size(); ++i) {
        if (!target(i)) continue;
        const Pre& p = pre[i];
        switch (f) {
            case Fmt::H1: case Fmt::H2: case Fmt::H3:
                all = all && p.kind == Kind::Heading &&
                      p.level == 1 + static_cast<int>(f) - static_cast<int>(Fmt::H1);
                break;
            case Fmt::Bullet:   all = all && p.kind == Kind::Bullet;   break;
            case Fmt::Numbered: all = all && p.kind == Kind::Numbered; break;
            case Fmt::Task:     all = all && p.kind == Kind::Task;     break;
            case Fmt::Quote:    all = all && p.quote;                  break;
            default:            all = false;                           break;
        }
    }

    // Each line becomes newPrefix + old.substr(cut). One shape for every verb,
    // so the cursor can be carried across by one rule below.
    std::vector<U>   nprefix(pre.size());
    std::vector<int> cut(pre.size(), 0);
    int number = 0;
    for (std::size_t i = 0; i < pre.size(); ++i) {
        if (!target(i)) continue;
        const Pre& p = pre[i];
        const U&   s = text[i];
        const U quote  = s.substr(0, static_cast<std::size_t>(p.qe));   // `> ` kept as written, or empty
        const U indent = s.substr(static_cast<std::size_t>(p.qe),
                                  static_cast<std::size_t>(p.ind_end - p.qe));
        switch (f) {
            case Fmt::H1: case Fmt::H2: case Fmt::H3: {
                const int lv = 1 + static_cast<int>(f) - static_cast<int>(Fmt::H1);
                nprefix[i] = all ? quote : quote + U(static_cast<std::size_t>(lv), U'#') + U(U" ");
                cut[i] = p.body;
                break;
            }
            case Fmt::Plain:
                cut[i] = p.body;   // every block mark and the indent go; the words stay
                break;
            case Fmt::Bullet: case Fmt::Task: case Fmt::Numbered: {
                // A heading is not indented; a list item keeps its nesting.
                const U ind = p.kind == Kind::Heading ? U() : indent;
                U mark;
                if (!all) {
                    if (f == Fmt::Bullet) mark = u("- ");
                    else if (f == Fmt::Task) mark = u(p.kind == Kind::Task && p.checked ? "- [x] " : "- [ ] ");
                    else mark = decode(std::to_string(++number) + ". ");
                }
                nprefix[i] = quote + ind + mark;
                cut[i] = p.body;
                break;
            }
            case Fmt::Quote:
                if (all) {
                    nprefix[i] = s.substr(0, static_cast<std::size_t>(p.q0));
                    cut[i] = p.qe;
                } else {
                    nprefix[i] = u("> ");
                    cut[i] = 0;
                }
                break;
            default: break;
        }
    }

    U out;
    std::vector<int> nstart(pre.size());
    for (std::size_t i = 0; i < pre.size(); ++i) {
        if (i) out += U'\n';
        nstart[i] = static_cast<int>(out.size());
        if (!target(i)) { out += text[i]; continue; }
        out += nprefix[i];
        out += text[i].substr(static_cast<std::size_t>(std::min<int>(cut[i], static_cast<int>(text[i].size()))));
    }

    const int r0 = L.start[static_cast<std::size_t>(la)], r1 = L.end(lb);
    // A position keeps its place in the words; one inside the old marks lands
    // at the start of the words. A position past the last touched line (a
    // selection that ended at the start of the next) moves with the length.
    const int delta = static_cast<int>(out.size()) - (r1 - r0);
    auto carry = [&](int p) {
        const int i = L.of(p);
        if (i > lb) return p + delta;
        const auto k = static_cast<std::size_t>(i - la);
        const int col = p - L.start[static_cast<std::size_t>(i)];
        if (!target(k)) return r0 + nstart[k] + col;
        // A selection that starts at a line's start keeps the whole line.
        if (a != b && col == 0) return r0 + nstart[k];
        const int np = static_cast<int>(nprefix[k].size());
        return r0 + nstart[k] + np + std::max(0, col - cut[k]);
    };
    return finish(t, r0, r1, {{r0, r1 - r0, out}}, carry(a), carry(b));
}

// ── a fenced block ───────────────────────────────────────────────────────────
bool is_fence(const U& s) {
    std::size_t i = 0;
    while (i < s.size() && i < 3 && s[i] == U' ') ++i;
    if (i + 3 > s.size()) return false;
    const char32_t c = s[i];
    return (c == U'`' || c == U'~') && s[i + 1] == c && s[i + 2] == c;
}

FmtEdit code_block(const U& t, int a, int b) {
    const Lines L = lines_of(t);
    int la = 0, lb = 0;
    touched(L, a, b, la, lb);
    auto line = [&](int i) {
        const int s = L.start[static_cast<std::size_t>(i)];
        return t.substr(static_cast<std::size_t>(s), static_cast<std::size_t>(L.end(i) - s));
    };

    // Inside a fence (or on one of its lines): take the fence away.
    int open = -1;
    for (int i = 0; i < L.count(); ++i) {
        if (!is_fence(line(i))) continue;
        if (open < 0) { open = i; continue; }
        if (open <= la && i >= lb) {
            const int r0 = L.start[static_cast<std::size_t>(open)], r1 = L.end(i);
            U inner;
            if (i - open > 1) {
                const int s = L.start[static_cast<std::size_t>(open) + 1];
                inner = t.substr(static_cast<std::size_t>(s), static_cast<std::size_t>(L.end(i - 1) - s));
            }
            return finish(t, r0, r1, {{r0, r1 - r0, inner}}, r0, r0 + static_cast<int>(inner.size()));
        }
        open = -1;
    }

    const int r0 = L.start[static_cast<std::size_t>(la)], r1 = L.end(lb);
    const U inner = t.substr(static_cast<std::size_t>(r0), static_cast<std::size_t>(r1 - r0));
    const U fence = u("```");
    const U out = fence + U(U"\n") + inner + U(U"\n") + fence;
    const int c0 = r0 + 4;
    return finish(t, r0, r1, {{r0, r1 - r0, out}}, c0, c0 + static_cast<int>(inner.size()));
}

// ── typing in a list (s025) ──────────────────────────────────────────────────
bool is_list(const Pre& p) {
    return p.kind == Kind::Bullet || p.kind == Kind::Task || p.kind == Kind::Numbered;
}

// Lines a fence owns are code: a `- x` there is text, never a list.
std::vector<bool> code_lines(const std::string& body) {
    const Scan sc = scan(body);
    std::vector<bool> out;
    out.reserve(sc.lines.size());
    for (const auto& ln : sc.lines) out.push_back(ln.block == Block::Code || ln.block == Block::Fence);
    return out;
}

U line_text(const U& t, const Lines& L, int i) {
    const int s = L.start[static_cast<std::size_t>(i)];
    return t.substr(static_cast<std::size_t>(s), static_cast<std::size_t>(L.end(i) - s));
}

int indent_of(const Pre& p) { return p.ind_end - p.qe; }

// Where a list line's indent should go. In: under the nearest item above at
// the SAME level, lined up with its words (so `1. ` nests by three, `- ` by
// two -- what markdown itself asks for). Out: to the level of the nearest item
// above that is LESS indented -- its parent. Blank lines are passed over; any
// other line ends the list and the search.
int target_indent(const U& t, const Lines& L, int line, const Pre& p, bool outdent) {
    const int cur = indent_of(p);
    for (int j = line - 1; j >= 0; --j) {
        const Pre q = parse(line_text(t, L, j));
        if (q.blank) continue;
        if (!is_list(q) || q.quote != p.quote) break;
        const int qi = indent_of(q);
        if (!outdent && qi == cur) {
            // A task's words start after `- [ ] `, but its children line up
            // under the `[`, as they would under a plain bullet's words.
            const int w = q.kind == Kind::Task ? 2 : q.body - q.ind_end;
            return cur + std::max(2, w);
        }
        if (!outdent && qi < cur) return cur;           // already this item's first child
        if (outdent && qi < cur) return qi;
    }
    return outdent ? 0 : cur + 2;
}

}  // namespace

FmtEdit indent(const std::string& body, int sel_begin, int sel_end, bool outdent) {
    const U t = decode(body);
    const int n = static_cast<int>(t.size());
    const int a = std::clamp(std::min(sel_begin, sel_end), 0, n);
    const int b = std::clamp(std::max(sel_begin, sel_end), 0, n);
    const Lines L = lines_of(t);
    const auto code = code_lines(body);
    int la = 0, lb = 0;
    touched(L, a, b, la, lb);

    // The first list line decides how far; every list line moves that far,
    // so a selected sub-list keeps its shape.
    int delta = 0;
    bool any = false;
    for (int i = la; i <= lb && !any; ++i) {
        if (static_cast<std::size_t>(i) < code.size() && code[static_cast<std::size_t>(i)]) continue;
        const Pre p = parse(line_text(t, L, i));
        if (!is_list(p)) continue;
        any = true;
        delta = target_indent(t, L, i, p, outdent) - indent_of(p);
    }
    if (!any) return {};

    std::vector<Ed> eds;
    for (int i = la; i <= lb; ++i) {
        if (static_cast<std::size_t>(i) < code.size() && code[static_cast<std::size_t>(i)]) continue;
        const Pre p = parse(line_text(t, L, i));
        if (!is_list(p)) continue;
        const int s = L.start[static_cast<std::size_t>(i)];
        const int now = indent_of(p), want = std::max(0, now + delta);
        if (want == now) continue;
        eds.push_back({s + p.qe, now, U(static_cast<std::size_t>(want), U' ')});   // tabs become spaces
    }
    const int r0 = L.start[static_cast<std::size_t>(la)], r1 = L.end(lb);
    // A position in the moved indent stays at the line's mark -- except the
    // start of a selection at a line's start, which keeps the whole line.
    auto carry = [&](int pos) {
        int d = 0;
        for (const auto& e : eds) {
            if (a != b && pos == a && pos == e.pos && e.pos == L.start[static_cast<std::size_t>(L.of(pos))])
                return pos + d;
            if (pos >= e.pos + e.del) d += static_cast<int>(e.ins.size()) - e.del;
            else if (pos > e.pos) return e.pos + d + static_cast<int>(e.ins.size());
        }
        return pos + d;
    };
    return finish(t, r0, r1, eds, carry(a), carry(b));
}

FmtEdit enter(const std::string& body, int sel_begin, int sel_end) {
    if (sel_begin != sel_end) return {};
    const U t = decode(body);
    const int a = std::clamp(sel_begin, 0, static_cast<int>(t.size()));
    const Lines L = lines_of(t);
    const int ln = L.of(a);
    const auto code = code_lines(body);
    if (static_cast<std::size_t>(ln) < code.size() && code[static_cast<std::size_t>(ln)]) return {};

    const U s = line_text(t, L, ln);
    const Pre p = parse(s);
    if (!is_list(p) && !p.quote) return {};
    if (p.kind == Kind::Heading) return {};
    const int ls = L.start[static_cast<std::size_t>(ln)];
    const int col = a - ls;
    const int mark_end = is_list(p) ? p.body : p.qe;
    if (col < mark_end) return {};      // inside the mark: an ordinary newline

    bool empty = true;
    for (int k = mark_end; k < static_cast<int>(s.size()); ++k)
        if (!is_space(s[static_cast<std::size_t>(k)])) { empty = false; break; }

    if (empty) {
        // Enter on an empty item. Nested: step out a level (and the cursor
        // stays on the item, ready to type). Top level: the mark goes, and
        // with it the list; a quote's `> ` goes last.
        if (is_list(p) && indent_of(p) > 0) {
            FmtEdit e = indent(body, a, a, true);
            if (e.ok) return e;
        }
        const U keep = is_list(p) ? s.substr(0, static_cast<std::size_t>(p.qe)) : U();
        const int r1 = L.end(ln);
        return {true, ls, r1, encode(keep), ls + static_cast<int>(keep.size()),
                ls + static_cast<int>(keep.size())};
    }

    U mark = s.substr(0, static_cast<std::size_t>(p.ind_end));   // `> ` and the indent, as written
    switch (p.kind) {
        case Kind::Bullet:   mark += U(1, p.bullet) + U(U" "); break;
        case Kind::Task:     mark += U(1, p.bullet) + U(U" [ ] "); break;
        case Kind::Numbered: mark += decode(std::to_string(p.num + 1)) + U(1, p.delim) + U(U" "); break;
        default: break;
    }
    // The words after the cursor go down with it; spaces left at the break
    // on either side are not worth keeping.
    int cut = a, rest = a;
    while (cut > ls + mark_end && is_space(t[static_cast<std::size_t>(cut - 1)])) --cut;
    while (rest < L.end(ln) && is_space(t[static_cast<std::size_t>(rest)])) ++rest;
    const U ins = U(U"\n") + mark;
    const int c = cut + static_cast<int>(ins.size());
    return {true, cut, rest, encode(ins), c, c};
}

FmtEdit format(const std::string& body, int sel_begin, int sel_end, Fmt f) {
    const U t = decode(body);
    const int n = static_cast<int>(t.size());
    int a = std::clamp(std::min(sel_begin, sel_end), 0, n);
    int b = std::clamp(std::max(sel_begin, sel_end), 0, n);
    switch (f) {
        case Fmt::Bold: case Fmt::Italic: case Fmt::Strike: case Fmt::Code:
            return inline_mark(t, a, b, f);
        case Fmt::Link:
            return link(t, a, b);
        case Fmt::CodeBlock:
            return code_block(t, a, b);
        default:
            return line_prefix(t, a, b, f);
    }
}

std::string apply(const std::string& body, const FmtEdit& e) {
    if (!e.ok) return body;
    U t = decode(body);
    t.replace(static_cast<std::size_t>(e.cp_begin), static_cast<std::size_t>(e.cp_end - e.cp_begin),
              decode(e.text));
    return encode(t);
}

}  // namespace jot::core
