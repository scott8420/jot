#pragma once
#include "core/Nodes.hpp"
#include "core/Prefs.hpp"

#include <cstddef>
#include <cstdint>
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
//
// s038b -- THE QUERY LANGUAGE, and perspectives. The same field takes more than
// words, so a saved filter is just a saved query -- one text, one truth; the
// filter menu beside the field is a VIEW over that text (it reads the tokens
// and writes them), never a second store:
//
//   paint shed          words: every one, in the name or the text (s038)
//   #errands            a tag: the note carries it, or one nested under it,
//                       or one that STARTS with it (so typing narrows, as the
//                       chips' search does)
//   #errands or #car    `or` joins the terms either side of it; everything
//                       else is AND. (#a or #b) and (paint or primer) -- no
//                       brackets, and none needed for what a perspective says.
//   -#someday           a leading minus: NOT this term
//   is:remaining  is:available  is:waiting  is:done  is:dropped  is:todo
//   is:note  is:project  is:inbox  is:flagged
//   due:overdue  due:today  due:week  due:any  due:none
//                       "due by": today and week include what is already late.
//                       A due: term (and is:flagged) is about what is left to
//                       do, so a ticked todo never matches one.
//
// A token that is not one of these is a word -- `is:foo` is looked for as
// typed. Unknown is not an error: it is just text nobody wrote.
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

// ── the query (s038b) ───────────────────────────────────────────────────────
enum class TermKind { Word, Tag, State, Due };
enum class StateTerm { Remaining, Available, Waiting, Done, Dropped, Todo, Note, Project, Inbox,
                       Flagged };
enum class DueTerm { Overdue, Today, Week, Any, None };

struct Term {
    TermKind    kind   = TermKind::Word;
    bool        negate = false;
    std::string text;                      // Word: lower-cased; Tag: the key ("home/garden")
    StateTerm   state  = StateTerm::Remaining;
    DueTerm     due    = DueTerm::Any;
};

// AND of clauses; each clause an OR of terms.
struct Query {
    std::vector<std::vector<Term>> clauses;
    bool empty() const { return clauses.empty(); }
    bool has_filters() const;   // anything beyond plain words: a tag, is:, due:, or, -
};

Query parse_query(const std::string& query);

// Is there anything to look for -- the field's "show results" test.
bool query_active(const std::string& query);

// Does one node pass. `now` is the caller's instant.
bool query_matches(const NodeSource& src, const Node& n, const Query& q, std::int64_t now);

// What the query says, in words, for the summary line: "Remaining todos ·
// #errands or #car · due within a week". "" for plain words (s038's summary
// already says it).
std::string describe_query(const std::string& query);

// The filter menu's view over the text. Each reads the FIRST plain (not or-ed,
// not negated) token of its kind -- the one the menu would show pressed.
//   query_show   "" (anything), "remaining", "available", "waiting", "done",
//                "dropped", "todo", "note", "project", "inbox"
//   query_due    "" (any time), "overdue", "today", "week", "any", "none"
//   query_flagged  is:flagged is there
std::string query_show(const std::string& query);
std::string query_due(const std::string& query);
bool        query_flagged(const std::string& query);
// And the writers: drop every plain token of that kind, then add the new one
// at the end ("" adds none). Spacing collapses to single spaces; nothing else
// in the text moves.
std::string query_set_show(const std::string& query, const std::string& show);
std::string query_set_due(const std::string& query, const std::string& due);
std::string query_set_flagged(const std::string& query, bool on);

// Every match, ranked as above, at most `cap` (the pane says when it stopped).
// `now` 0 reads the clock (the GTK side); the selftest passes its own.
std::vector<SearchHit> search(const NodeSource& src, const std::string& query,
                              std::size_t cap = 200, std::int64_t now = 0);

// How many notes matched in all -- for "200 of 512" when capped.
std::size_t search_count(const NodeSource& src, const std::string& query, std::int64_t now = 0);

// ── perspectives (s038b) ────────────────────────────────────────────────────
// A perspective is a NAME for a query (Prefs::perspectives, app-wide). Two
// queries are the same perspective when they read the same: case and spacing
// do not count.
std::string query_norm(const std::string& query);
const Perspective* perspective_for(const std::vector<Perspective>& list, const std::string& query);
const Perspective* perspective_named(const std::vector<Perspective>& list, const std::string& name);
// Save under `name` (trimmed): replaces one of the same name (case ignored),
// else adds; kept sorted by name. False for an empty name or a query with
// nothing to look for.
bool perspective_save(std::vector<Perspective>& list, const std::string& name,
                      const std::string& query);
bool perspective_remove(std::vector<Perspective>& list, const std::string& name);

}  // namespace jot::core
