#pragma once
#include "core/Format.hpp"
#include "core/Nodes.hpp"
#include "core/Tasks.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Tags -- tags as contexts (s035, road item 7).
//
// THE BODY IS THE SOURCE. Scott's call (Oct 2026): a tag is `#errands` typed in
// the note's text, the Obsidian way, so a note carries its tags with it into
// any other markdown tool. Nothing here stores a tag. Every answer below is a
// query over core::scan of the bodies -- the same scan the editor colours and
// the drawer lists, so "is this a tag" has exactly one answer in the app.
//
// What OmniFocus calls a context (a tag perspective: "everything I can do
// @errands") is then a FILTER, not a field: pick a tag, see its available
// todos, the ones waiting, and the notes that mention it.
//
// THREE RULES, each a choice worth writing down:
//
//   case folds    #Errands and #errands are one tag. Two tags that differ only
//                 in case are a typo, not a distinction anyone meant. The KEY is
//                 ASCII-lowercased; the name shown is the first spelling met in
//                 tree order, so the chip reads the way you usually write it.
//
//   nesting       #home/garden sits under #home, as in Obsidian. Picking #home
//                 shows #home/garden's notes too; picking #home/garden does not
//                 show plain #home. A parent level that is never written by
//                 itself still gets a chip -- it is how you reach the family.
//
//   no inheritance  A tag belongs to the note whose text carries it. A project
//                 tagged #work does not tag its steps: OmniFocus does not
//                 inherit tags either, and an inherited tag is one you cannot
//                 see in the note it applies to -- the drawer would show a tag
//                 the text does not contain.
//
// GTK-free; the selftest owns every verdict here.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// The fold: ASCII-lowercased, trailing '/' dropped. "Home/Garden/" -> "home/garden".
std::string tag_key(std::string_view name);

// Does a node tagged `key` belong to the filter `filter`? Equal, or nested
// beneath it ("home/garden" is under "home"; "homework" is not).
bool tag_under(std::string_view key, std::string_view filter);

// One node's tags as KEYS, de-duplicated, in the order the text has them.
std::vector<std::string> node_tag_keys(const Node& n);

// One chip in the Tags view.
struct TagInfo {
    std::string key;            // "home/garden"
    std::string name;           // "Home/garden" -- first spelling, tree order
    int         depth = 0;      // slashes in the key: 0 for "home", 1 for "home/garden"
    bool        written = true; // false: only implied by a nested tag beneath it
    std::size_t notes = 0;      // nodes under the filter that are not todos
    std::size_t open  = 0;      // todos not done / dropped -- available or waiting
    std::size_t available = 0;  // of those, actionable now
};

// Every tag in the folder, parents implied by nesting included, sorted by key
// (so "home" sits just before "home/garden"). Counts are DISTINCT nodes under
// the filter, nesting included -- the number on a chip is how many rows
// picking it shows.
std::vector<TagInfo> tag_list(const NodeSource& src, std::int64_t now);

// ── the search over the chips (s036) ────────────────────────────────────────
// Scott (Oct 2026): "a search/filtering to present only the tags wanted". What
// is typed is folded like a key (case, spaces, leading #s) and matched ANYWHERE
// in a chip's key: `ho` finds #home, #home/garden and #phone. An empty query
// matches every chip.
std::string tag_query(std::string_view typed);
bool tag_matches(std::string_view key, std::string_view query);

// Enter in the search field: the key to pick -- the chip whose key IS the
// query, else the only chip that matches, else "" (several, or none).
std::string tag_pick(const std::vector<TagInfo>& list, std::string_view typed);

// What picking a tag shows, every list in TREE ORDER.
struct TagMembers {
    std::vector<NodeId> available;   // todos you can do now
    std::vector<NodeId> waiting;     // todos deferred, blocked or on hold
    std::vector<NodeId> notes;       // tagged nodes that are not todos
    std::vector<NodeId> done;        // s035b: done or dropped todos -- the pane folds them away
    std::size_t         finished = 0;   // == done.size()
};
TagMembers tag_members(const NodeSource& src, std::string_view filter, std::int64_t now);

// Why a waiting todo waits, in a few words for its row: "Deferred until
// 2026-10-09", "Blocked: an earlier step is open", "On hold". Empty when it is
// not waiting.
std::string waiting_reason(const NodeSource& src, const NodeId& id, std::int64_t now);

// ── the tag line (s035b) ────────────────────────────────────────────────────
// Scott (Oct 2026): tags are details of the note, edited in Note details --
// and still TEXT, so the note stays portable. The home for them is the TAG
// LINE: the note's last non-blank line when it holds nothing but #tags
// (`#errands #home/garden`). Note details adds to it and takes from it; the
// editor draws it as a band in Live Preview and Reading. Bottom, not top:
// the FIRST line is already the note's why (Today), a capture's name and an
// import's title.
//
// A #tag anywhere else in the text still tags the note -- it is just not on
// the line, so Note details shows it as "in text" and cannot remove it.
struct TagLine {
    bool found = false;
    int  line  = -1;                    // scan line index
    int  begin = 0, end = 0;            // bytes, newline not included
    int  cp_begin = 0, cp_end = 0;
    std::vector<std::string> names;     // as written, in order
};
TagLine find_tag_line(const std::string& body);

// What a person typed into the tag field, made a tag name: spaces trimmed,
// leading #s dropped, inner spaces -> '-', trailing '/', '.', '-' dropped.
// "" when what is left is not a tag the scanner would read (must start with a
// letter; letters, digits, '-', '_', '/').
std::string clean_tag_name(std::string_view typed);

// The edit that ADDS a tag to the tag line, making the line (after a blank
// line) when there is none. ok == false when the name is not a tag or the
// note already carries it anywhere (by key).
FmtEdit tag_add_edit(const std::string& body, std::string_view name);

// The edit that REMOVES a tag from the tag line. When it was the last one the
// line goes, with the blank lines before it. ok == false when the tag is not
// on the line (a tag "in text" is removed where you wrote it).
FmtEdit tag_remove_edit(const std::string& body, std::string_view name);

}  // namespace jot::core
