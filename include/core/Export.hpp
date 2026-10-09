#pragma once
// core::Export -- notes out of jot as markdown anyone can read (s071d).
//
// jot's own files are for jot: uuid names, the tree and every date in
// jot.json. Export is what makes them readable anywhere -- Obsidian, VS Code,
// GitHub, a text editor -- and it is shaped like an OUTLINE (Scott, Oct 8:
// "top levels are files and included notes and tasks are inside. Think of an
// outline of the object"):
//
//   * each exported top-level note is ONE file, named by its title, its own
//     facts (todo, due, project, tags ...) in front matter;
//   * the notes under it are headings, one level deeper per level (past six,
//     bold bullets);
//   * the todos under it are checkbox lines, their facts written out on the
//     line ("— due Fri 9 Oct 17:00 · ⚑ · ~30m · #errands"), their text
//     indented under them, their own steps nested;
//   * a note's own headings are pushed down under its level, so the outline
//     stays an outline; a first heading that only repeats the title goes;
//   * `[label](jot:<id>)` links point at the exported file that holds the
//     target, or become plain text when it was not exported;
//   * pictures and files (`attachments/...`) are copied beside the files, so
//     the same relative links work.
//
// GTK-free: every byte is decided here and proved headless.
#include "core/Nodes.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace jot::core {

// "Move house" -> "Move house.md"; characters no file system likes become
// "-", leading dots and spaces go, long titles are cut, "" is "Untitled".
std::string export_file_name(const std::string& title);

// The selection, ready to export: a note under another selected note is
// already inside that one's file, so it is dropped. Order kept.
std::vector<NodeId> export_roots(const NodeSource& src, const std::vector<NodeId>& selected);

// The text of ONE root's file. `file_of` maps every exported node's id to the
// file holding it (for the links). `now` decides whether a date needs its year.
std::string export_outline(const NodeSource& src, const NodeId& root,
                           const std::map<NodeId, std::string>& file_of, std::int64_t now);

// The `attachments/...` names a text refers to ("attachments/a.png" -> "a.png").
std::vector<std::string> export_attachment_refs(const std::string& text);

struct ExportResult {
    std::vector<std::string> files;      // the .md files written, in order
    int notes = 0;                       // every note and todo inside them
    int attachments = 0;                 // files copied into attachments/
    std::vector<std::string> problems;   // what could not be written or found
};

// Write each root's outline into `dest` (made if missing), a name taken
// ("Name 2.md") never overwritten, and copy the attachments they use from
// `attach_dir` into dest/attachments/.
//
// s071d (Scott: "allow a new name and not always use its title but it can be
// the initial name"): `names`, when given, is the file name for each root in
// the same order -- "" keeps the title. A name given is the user's choice
// from a Save dialog (which already asked about replacing), so it is written
// as given, ".md" added when missing.
ExportResult export_to(const NodeSource& src, const std::vector<NodeId>& roots,
                       const std::string& dest, const std::string& attach_dir, std::int64_t now,
                       const std::vector<std::string>& names = {});

// A folder name free inside `parent`: "jot export 2026-10-08", then " 2"...
std::string export_folder_name(const std::string& parent, const std::string& stem, std::int64_t now);

// `name` if nothing in `parent` has it, else "name 2", "name 3"...
std::string export_free_name(const std::string& parent, const std::string& name);

}  // namespace jot::core
