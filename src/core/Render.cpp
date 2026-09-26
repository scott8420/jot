#include "core/Render.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace jot::core {
namespace {

bool is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

// The Mark span a list line's marker makes: it starts where the line's
// indentation ends. Returns its end, or -1.
int marker_end(const Scan& sc, int at) {
    int end = -1;
    for (const Span& s : sc.spans)
        if (s.style == Style::Mark && s.begin == at) end = std::max(end, s.end);
    return end;
}

// What goes in front of a source byte, and what it is for.
struct Insert {
    std::string text;
    enum class For { Glyph, Box, Anchor } what = For::Glyph;
    int index = -1;              // into boxes / anchors
};

}  // namespace

Rendered render(const std::string& body) {
    Rendered r;
    const Scan sc = scan(body);
    const int n = static_cast<int>(body.size());

    std::vector<char> hide(static_cast<std::size_t>(n), 0);
    auto hide_range = [&](int b, int e) {
        for (int i = std::max(0, b); i < std::min(e, n); ++i) hide[static_cast<std::size_t>(i)] = 1;
    };
    auto show_range = [&](int b, int e) {
        for (int i = std::max(0, b); i < std::min(e, n); ++i) hide[static_cast<std::size_t>(i)] = 0;
    };
    for (const Span& s : sc.spans)
        if (s.style == Style::Mark) hide_range(s.begin, s.end);

    std::map<int, Insert> ins;   // source byte -> what goes in front of it

    for (int li = 0; li < static_cast<int>(sc.lines.size()); ++li) {
        const Line& ln = sc.lines[static_cast<std::size_t>(li)];
        int at = ln.begin;
        while (at < ln.end && (body[static_cast<std::size_t>(at)] == ' ' ||
                               body[static_cast<std::size_t>(at)] == '\t')) ++at;
        switch (ln.block) {
            case Block::Bullet:
                if (marker_end(sc, at) > at) ins[at] = {"• ", Insert::For::Glyph, -1};
                break;
            case Block::Task:
                if (marker_end(sc, at) > at) {
                    r.boxes.push_back(RenderBox{0, li, ln.checked});
                    ins[at] = {ln.checked ? "☑ " : "☐ ", Insert::For::Box,
                               static_cast<int>(r.boxes.size()) - 1};
                }
                break;
            case Block::Numbered: {
                const int me = marker_end(sc, at);
                if (me > at) show_range(at, me);   // the number is content
                break;
            }
            case Block::Fence: {
                // s021b: an OPENING fence swallows its whole block -- code
                // lines and the closing fence -- into one anchor. The newline
                // after the block stays, so what follows starts a line.
                int last = li;
                std::string code;
                int k = li + 1;
                for (; k < static_cast<int>(sc.lines.size()); ++k) {
                    const Line& c = sc.lines[static_cast<std::size_t>(k)];
                    if (c.block == Block::Fence) { last = k; break; }
                    if (k > li + 1) code += "\n";
                    code += body.substr(static_cast<std::size_t>(c.begin),
                                        static_cast<std::size_t>(c.end - c.begin));
                    last = k;
                }
                std::string lang = body.substr(static_cast<std::size_t>(at),
                                               static_cast<std::size_t>(ln.end - at));
                while (!lang.empty() && (lang.front() == '`' || lang.front() == '~')) lang.erase(0, 1);
                while (!lang.empty() && std::isspace(static_cast<unsigned char>(lang.front()))) lang.erase(0, 1);
                while (!lang.empty() && std::isspace(static_cast<unsigned char>(lang.back()))) lang.pop_back();
                const Line& end_ln = sc.lines[static_cast<std::size_t>(last)];
                hide_range(ln.begin, end_ln.end);
                RenderAnchor a{RenderAnchor::Kind::Code, 0, {}, {}, std::move(code), std::move(lang)};
                r.anchors.push_back(std::move(a));
                ins[ln.begin] = {kAnchorChar, Insert::For::Anchor,
                                 static_cast<int>(r.anchors.size()) - 1};
                li = last;   // the block's lines are consumed
                break;
            }
            case Block::Rule:
                hide_range(ln.begin, ln.end);
                r.anchors.push_back(RenderAnchor{RenderAnchor::Kind::Rule, 0, {}, {}, {}, {}});
                ins[ln.begin] = {kAnchorChar, Insert::For::Anchor,
                                 static_cast<int>(r.anchors.size()) - 1};
                break;
            default:
                break;
        }
    }

    // Links: the label stays, the marks went with the Mark spans. An IMAGE goes
    // entirely and an anchor takes its place.
    struct LabelRange { int b, e; std::string target; };
    std::vector<LabelRange> labels;
    for (const Link& lk : sc.links) {
        if (lk.image) {
            hide_range(lk.begin, lk.end);
            r.anchors.push_back(RenderAnchor{RenderAnchor::Kind::Image, 0, lk.target, lk.label, {}, {}});
            ins[lk.begin] = {kAnchorChar, Insert::For::Anchor,
                             static_cast<int>(r.anchors.size()) - 1};
        } else {
            const int lb = lk.begin + 1;
            labels.push_back({lb, lb + static_cast<int>(lk.label.size()), lk.target});
        }
    }

    // ── one walk: build the text and both maps ─────────────────────────────
    // src2ren[p] = where source byte p lands in the render (AFTER anything
    // inserted in front of it); ren_src[k] = the source byte rendered byte k
    // came from.
    std::vector<int> src2ren(static_cast<std::size_t>(n) + 1, 0);
    std::vector<int> ren_src;
    std::map<int, int> ins_at;   // source byte -> rendered byte its insert starts at
    r.text.reserve(body.size());
    for (int p = 0; p <= n; ++p) {
        if (auto it = ins.find(p); it != ins.end()) {
            ins_at[p] = static_cast<int>(r.text.size());
            r.text += it->second.text;
            ren_src.insert(ren_src.end(), it->second.text.size(), p);
        }
        src2ren[static_cast<std::size_t>(p)] = static_cast<int>(r.text.size());
        if (p < n && !hide[static_cast<std::size_t>(p)]) {
            r.text.push_back(body[static_cast<std::size_t>(p)]);
            ren_src.push_back(p);
        }
    }
    const int rn = static_cast<int>(r.text.size());

    // bytes -> codepoints, both sides
    std::vector<int> scp(static_cast<std::size_t>(n) + 1, 0);
    for (int i = 0, c = 0; i <= n; ++i) {
        scp[static_cast<std::size_t>(i)] = c;
        if (i < n && !is_cont(static_cast<unsigned char>(body[static_cast<std::size_t>(i)]))) ++c;
    }
    // A cp boundary in the source is a byte that is not a continuation; the
    // count BEFORE byte i is scp[i] only when i is a boundary, which every
    // offset we look up is.
    std::vector<int> rcp(static_cast<std::size_t>(rn) + 1, 0);
    for (int i = 0, c = 0; i <= rn; ++i) {
        rcp[static_cast<std::size_t>(i)] = c;
        if (i < rn && !is_cont(static_cast<unsigned char>(r.text[static_cast<std::size_t>(i)]))) {
            r.src_cp.push_back(scp[static_cast<std::size_t>(ren_src[static_cast<std::size_t>(i)])]);
            ++c;
        }
    }
    r.src_cp.push_back(scp[static_cast<std::size_t>(n)]);

    for (const Span& s : sc.spans) {
        if (s.style == Style::Mark) continue;
        const int b = src2ren[static_cast<std::size_t>(s.begin)];
        const int e = src2ren[static_cast<std::size_t>(s.end)];
        if (e <= b) continue;
        r.spans.push_back(Span{s.style, b, e, rcp[static_cast<std::size_t>(b)],
                               rcp[static_cast<std::size_t>(e)]});
    }
    for (const auto& lr : labels) {
        const int b = src2ren[static_cast<std::size_t>(lr.b)];
        const int e = src2ren[static_cast<std::size_t>(lr.e)];
        if (e > b)
            r.links.push_back(RenderLink{rcp[static_cast<std::size_t>(b)],
                                         rcp[static_cast<std::size_t>(e)], lr.target});
    }
    for (const auto& [p, in] : ins) {
        const int cp = rcp[static_cast<std::size_t>(ins_at[p])];
        if (in.what == Insert::For::Box) r.boxes[static_cast<std::size_t>(in.index)].cp = cp;
        if (in.what == Insert::For::Anchor) r.anchors[static_cast<std::size_t>(in.index)].cp = cp;
    }
    // Map order, which is source order, is what the view wants for anchors.
    std::sort(r.anchors.begin(), r.anchors.end(),
              [](const RenderAnchor& a, const RenderAnchor& b) { return a.cp < b.cp; });
    return r;
}

int source_cp(const Rendered& r, int rendered_cp) {
    if (r.src_cp.empty()) return 0;
    const int last = static_cast<int>(r.src_cp.size()) - 1;
    return r.src_cp[static_cast<std::size_t>(std::clamp(rendered_cp, 0, last))];
}

int rendered_cp(const Rendered& r, int source_cp) {
    if (r.src_cp.empty()) return 0;
    const auto it = std::lower_bound(r.src_cp.begin(), r.src_cp.end(), source_cp);
    const int last = static_cast<int>(r.src_cp.size()) - 1;
    return std::min(static_cast<int>(it - r.src_cp.begin()), last);
}

// ─────────────────────────────────────────────────────────────────────────────
// live_view (s022, s023) -- what Live Preview hides, and what it draws.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
bool blank_bytes(const std::string& t, int b, int e) {
    for (int k = b; k < e; ++k)
        if (!std::isspace(static_cast<unsigned char>(t[static_cast<std::size_t>(k)]))) return false;
    return true;
}
}  // namespace

LiveView live_view(const Scan& sc, const std::string& text, int reveal_first, int reveal_last) {
    LiveView v;
    const int nl = static_cast<int>(sc.lines.size());
    if (nl == 0) return v;
    auto revealed = [&](int a, int b) { return a <= reveal_last && b >= reveal_first; };

    // ── fenced blocks: pair the fence lines ────────────────────────────────
    std::vector<int> in_block(static_cast<std::size_t>(nl), -1);   // line -> open fence line
    for (int l = 0; l < nl; ++l) {
        if (sc.lines[static_cast<std::size_t>(l)].block != Block::Fence) continue;
        int close = -1;
        for (int k = l + 1; k < nl; ++k)
            if (sc.lines[static_cast<std::size_t>(k)].block == Block::Fence) { close = k; break; }
        const int end = close >= 0 ? close : nl - 1;
        for (int k = l; k <= end; ++k) in_block[static_cast<std::size_t>(k)] = l;

        const Line& open = sc.lines[static_cast<std::size_t>(l)];
        const int first_code = l + 1;
        const int last_code  = (close >= 0 ? close : nl) - 1;
        if (!revealed(l, end)) {
            v.hidden.push_back({open.cp_begin, open.cp_end + 1});   // the fence and its newline
            if (close >= 0) {
                const Line& c = sc.lines[static_cast<std::size_t>(close)];
                if (close < nl - 1) v.hidden.push_back({c.cp_begin, c.cp_end + 1});
                else v.hidden.push_back({c.cp_begin - 1, c.cp_end});   // last line: the newline BEFORE it
            }
            if (last_code >= first_code) {
                LiveDeco d;
                d.kind = LiveDeco::Kind::Code;
                d.line = first_code;
                const int cb = sc.lines[static_cast<std::size_t>(first_code)].begin;
                const int ce = sc.lines[static_cast<std::size_t>(last_code)].end;
                d.code = text.substr(static_cast<std::size_t>(cb), static_cast<std::size_t>(ce - cb));
                int k = open.begin;
                while (k < open.end && std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(k)]))) ++k;
                const char f = k < open.end ? text[static_cast<std::size_t>(k)] : '`';
                while (k < open.end && text[static_cast<std::size_t>(k)] == f) ++k;
                int e = open.end;
                while (k < e && std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(k)]))) ++k;
                while (e > k && std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(e - 1)]))) --e;
                d.lang = text.substr(static_cast<std::size_t>(k), static_cast<std::size_t>(e - k));
                v.decos.push_back(std::move(d));
            }
        }
        l = end;
    }

    // ── images alone on their line ─────────────────────────────────────────
    for (const Link& lk : sc.links) {
        if (!lk.image || lk.line < 0 || lk.line >= nl) continue;
        if (in_block[static_cast<std::size_t>(lk.line)] >= 0 || revealed(lk.line, lk.line)) continue;
        const Line& ln = sc.lines[static_cast<std::size_t>(lk.line)];
        if (!blank_bytes(text, ln.begin, lk.begin) || !blank_bytes(text, lk.end, ln.end)) continue;
        LiveDeco d;
        d.kind   = LiveDeco::Kind::Image;
        d.line   = lk.line;
        d.cp     = lk.cp_begin;
        d.cp_end = lk.cp_end;
        d.target = lk.target;
        d.label  = lk.label;
        v.decos.push_back(std::move(d));
    }

    // ── the marks ──────────────────────────────────────────────────────────
    for (const Span& s : sc.spans) {
        if (s.style != Style::Mark || s.cp_end <= s.cp_begin) continue;
        // The line the mark sits on: the last line starting at or before it.
        auto it = std::upper_bound(sc.lines.begin(), sc.lines.end(), s.cp_begin,
                                   [](int cp, const Line& ln) { return cp < ln.cp_begin; });
        if (it == sc.lines.begin()) continue;
        --it;
        const int line = static_cast<int>(it - sc.lines.begin());
        if (in_block[static_cast<std::size_t>(line)] >= 0) continue;   // done above
        if (revealed(line, line)) continue;
        const Line& ln = *it;
        const bool leading = s.cp_begin == ln.cp_begin + ln.level;
        if (ln.block == Block::Rule) {
            v.hidden.push_back({s.cp_begin, s.cp_end});
            LiveDeco d;
            d.kind = LiveDeco::Kind::Rule;
            d.line = line;
            d.cp   = ln.cp_begin;
            v.decos.push_back(d);
            continue;
        }
        if (leading && ln.block == Block::Numbered) continue;   // the number is content
        if (leading && (ln.block == Block::Bullet || ln.block == Block::Task)) {
            v.hidden.push_back({s.cp_begin, s.cp_end});
            LiveDeco d;
            d.kind    = ln.block == Block::Task ? LiveDeco::Kind::Box : LiveDeco::Kind::Bullet;
            d.line    = line;
            d.cp      = s.cp_end;
            d.checked = ln.checked;
            v.decos.push_back(d);
            v.hang_lines.push_back(line);
            continue;
        }
        bool in_image = false;
        for (const Link& lk : sc.links)
            if (lk.image && s.cp_begin >= lk.cp_begin && s.cp_end <= lk.cp_end) {
                in_image = true;
                break;
            }
        if (in_image) continue;   // an image is hidden whole or not at all -- the editor's call
        v.hidden.push_back({s.cp_begin, s.cp_end});
    }

    // Sorted and merged, so the editor applies each run once.
    std::sort(v.hidden.begin(), v.hidden.end(),
              [](const CpRange& a, const CpRange& b) { return a.begin < b.begin; });
    std::vector<CpRange> merged;
    for (const CpRange& r : v.hidden) {
        if (r.end <= r.begin) continue;
        if (!merged.empty() && r.begin <= merged.back().end)
            merged.back().end = std::max(merged.back().end, r.end);
        else
            merged.push_back(r);
    }
    v.hidden = std::move(merged);
    std::sort(v.decos.begin(), v.decos.end(),
              [](const LiveDeco& a, const LiveDeco& b) { return a.line < b.line; });
    return v;
}

}  // namespace jot::core
