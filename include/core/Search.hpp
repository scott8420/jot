#pragma once
#include "core/Nodes.hpp"

#include <cstddef>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Search -- road item 9, first half (s038): find text across every note.
//
// The query is words. A note matches when EVERY word is somewhere in it --
// its name or its text -- in any order, case ignored (the cheat sheet's rule,
// so jot's two search fields behave alike). `#errands` is just a word, so a
// tag search is a text search: tags ARE text (s035).
//
// Ranked in two groups, each in tree order: notes whose NAME holds every word
// first (you were probably typing a name), then the rest. Each hit carries the
// line of the body where it first matches, cut to fit a row, and where that is
// in the body in codepoints -- so a click can put the cursor on it.
//
// GTK-free. Bodies are in memory once a folder is open (Project loads them at
// open), so a search is a scan, not a read.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

struct SearchHit {
    NodeId      id;
    bool        in_name = false;   // every word is in the name
    std::string snippet;           // the body line it matched on, trimmed; "" when only the name matched
    int         cp_start = -1;     // codepoint offset of the first body match, -1 none
    int         cp_len   = 0;      // its length in codepoints
};

// Lower-cased words of a query; empty when there is nothing to look for.
std::vector<std::string> search_words(const std::string& query);

// Every match, ranked as above, at most `cap` (the pane says when it stopped).
std::vector<SearchHit> search(const NodeSource& src, const std::string& query,
                              std::size_t cap = 200);

// How many notes matched in all -- for "200 of 512" when capped.
std::size_t search_count(const NodeSource& src, const std::string& query);

}  // namespace jot::core
