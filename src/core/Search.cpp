#include "core/Search.hpp"

#include <functional>
#include <sstream>

// core/Search.cpp -- see the header. ASCII case-folding: the byte-wise lower
// keeps UTF-8 offsets valid and matches the cheat sheet's search; a word in
// another script matches as typed.

namespace jot::core {
namespace {

std::string lower(const std::string& s) {
    std::string o = s;
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return o;
}

int cp_count(const std::string& s, std::size_t from, std::size_t to) {
    int n = 0;
    for (std::size_t i = from; i < to && i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    return n;
}

// Back off to a codepoint boundary, so a cut never splits a character.
std::size_t boundary(const std::string& s, std::size_t i) {
    while (i > 0 && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return i;
}

// The line holding byte `at`, trimmed, cut to about `width` bytes around the
// hit so the matched word is always in view.
std::string snippet_at(const std::string& body, std::size_t at, std::size_t width = 90) {
    std::size_t b = body.rfind('\n', at);
    b = (b == std::string::npos) ? 0 : b + 1;
    std::size_t e = body.find('\n', at);
    if (e == std::string::npos) e = body.size();
    while (b < e && (body[b] == ' ' || body[b] == '\t')) ++b;
    while (e > b && (body[e - 1] == ' ' || body[e - 1] == '\t' || body[e - 1] == '\r')) --e;
    std::string pre, post;
    if (e - b > width) {
        std::size_t from = at > b + width / 3 ? at - width / 3 : b;
        from = boundary(body, from);
        std::size_t to = boundary(body, std::min(e, from + width));
        if (from > b) pre = "…";
        if (to < e) post = "…";
        b = from;
        e = to;
    }
    return pre + body.substr(b, e - b) + post;
}

void walk(const NodeSource& src, const NodeId& parent,
          const std::function<void(const Node&)>& fn) {
    for (const auto& id : src.children(parent)) {
        if (const Node* n = src.find(id)) fn(*n);
        walk(src, id, fn);
    }
}

// Does the node match; fills the hit if so.
bool match(const Node& n, const std::vector<std::string>& words, SearchHit& hit) {
    const std::string name = lower(n.title);
    const std::string body = lower(n.body);
    bool all_in_name = true;
    std::size_t first = std::string::npos, first_len = 0;
    for (const auto& w : words) {
        const bool in_name = name.find(w) != std::string::npos;
        const std::size_t at = body.find(w);
        if (!in_name && at == std::string::npos) return false;
        all_in_name = all_in_name && in_name;
        if (at != std::string::npos && (first == std::string::npos || at < first)) {
            first = at;
            first_len = w.size();
        }
    }
    hit.id = n.id;
    hit.in_name = all_in_name;
    if (first != std::string::npos) {
        hit.snippet  = snippet_at(n.body, first);
        hit.cp_start = cp_count(n.body, 0, first);
        hit.cp_len   = cp_count(n.body, first, first + first_len);
    }
    return true;
}

}  // namespace

std::vector<std::string> search_words(const std::string& query) {
    std::vector<std::string> out;
    std::istringstream in(lower(query));
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::vector<SearchHit> search(const NodeSource& src, const std::string& query, std::size_t cap) {
    const auto words = search_words(query);
    std::vector<SearchHit> names, rest;
    if (words.empty()) return {};
    walk(src, "", [&](const Node& n) {
        SearchHit h;
        if (match(n, words, h)) (h.in_name ? names : rest).push_back(std::move(h));
    });
    names.insert(names.end(), std::make_move_iterator(rest.begin()),
                 std::make_move_iterator(rest.end()));
    if (names.size() > cap) names.resize(cap);   // ranked first, then cut: a name hit is never lost to the cap
    return names;
}

std::size_t search_count(const NodeSource& src, const std::string& query) {
    const auto words = search_words(query);
    if (words.empty()) return 0;
    std::size_t n = 0;
    walk(src, "", [&](const Node& node) {
        SearchHit h;
        if (match(node, words, h)) ++n;
    });
    return n;
}

}  // namespace jot::core
