#pragma once
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Import -- a markdown file from anywhere becomes a note (s021b).
//
// Scott: "there needs to be an easy way to import a markdown file." Two roads
// in, one answer: Notes -> Import Markdown... and a .md dropped from Files onto
// the tree. Both end here, so the two cannot disagree about what a file becomes.
//
// The body comes in AS WRITTEN (a BOM dropped, CRLF made LF -- nothing else),
// because the markdown is the truth and jot's job is to show it, not to
// reinterpret it. Two things are decided on the way in:
//   * the TITLE -- the first `# Heading`, else the file's name without .md;
//   * PICTURES -- a relative `![..](img/x.png)` means "next to the file", and
//     that meaning dies the moment the text leaves the file's folder. Each one
//     that names a file that is really there becomes a `file://` link, so it
//     shows in Reading as a LINKED enclosure. Anything else is left alone.
//
// GTK-free. Selftested.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

struct ImportedNote {
    std::string title;
    std::string body;
    int         pictures = 0;   // relative image targets rewritten to file://
};

// Is this a file the importer takes? .md / .markdown / .txt, any case.
bool is_markdown_filename(const std::string& name);

// The title for a body that came from `path`.
std::string import_title(const std::string& body, const std::string& path);

// Rewrite relative (and bare absolute) image targets that resolve, against
// `base_dir`, to an existing file. Returns the new body; `n` counts rewrites.
std::string localize_images(const std::string& body, const std::string& base_dir, int* n = nullptr);

// Read `path` and make a note of it. False, with `err`, for a folder, an
// unreadable file, or bytes that are not UTF-8 text (a buffer would refuse
// them anyway, silently).
bool import_markdown(const std::string& path, ImportedNote& out, std::string& err);

// ── a whole folder (s021c) ────────────────────────────────────────────────
// Scott: "if the user selects a folder then all md files inside the folder is
// imported". The folder becomes a parent note named after it; each markdown
// file a child; each subfolder that holds markdown (at any depth) a nested
// parent. Hidden entries (`.obsidian`, `.git`, `.trash`) are skipped, symlinked
// folders are not followed (a loop would never end), and a folder with no
// markdown anywhere under it is left out rather than imported as an empty
// shell. Names sort case-insensitively, folders before files, so the tree
// reads the way the folder does in Files.
struct ImportItem {
    std::string              title;      // a file's stem, or a folder's name
    std::string              path;       // absolute; a file, or a folder
    bool                     folder = false;
    std::vector<ImportItem>  children;   // folders only
};

// The plan for `dir`: its own item (a folder) with everything under it. False
// if `dir` is not a folder or holds no markdown at all. `limit` caps the files
// taken, so a mistaken import of `/home` stops rather than runs for an hour;
// `truncated` says it did.
bool plan_folder_import(const std::string& dir, ImportItem& out, int limit = 2000,
                        bool* truncated = nullptr);

// How many FILES a plan imports.
int count_files(const ImportItem& item);

}  // namespace jot::core
