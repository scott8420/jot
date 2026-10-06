#pragma once
#include "core/Enclosures.hpp"
#include "core/Packet.hpp"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Gather (s051, J2) -- GATHER FOR SENDING.
//
// Scott: "one place, one time". A packet whose items are all in is sent as
// ONE act: every file sitting on its item lines is copied into one folder (or
// one zip), named so the person receiving it can read it without jot --
//
//     Taxes 2026 - 2026-10-06/
//         00 Contents.txt
//         01 W-2 (employer).pdf
//         02 1099-INT (bank).pdf
//         04 HSA 5498 - 1.pdf
//         04 HSA 5498 - 2.jpg
//
// -- the number is the item's place in the list, so a paper item (ticked, no
// file) leaves a gap that Contents.txt explains: "paper copy -- not in here".
// The attachment's own name (a slug, or a pasted-20261006 stamp) is jot's,
// not the accountant's; the ITEM's label is what both of you call it.
//
// GTK-free. The plan is pure (selftested name by name); the two writers touch
// the disk and build in a temporary place first, so a failure part way never
// leaves a half packet where the user will find it.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

struct GatherFile {
    std::string from;   // absolute source path
    std::string as;     // the name inside the folder / zip
    int         item = 0;   // index into PacketState::items
};

struct GatherPlan {
    std::string              name;       // folder name / zip stem: "Taxes 2026 - 2026-10-06"
    std::vector<GatherFile>  files;
    std::string              contents;   // the text of "00 Contents.txt"
    std::vector<std::string> problems;   // "W-2 (employer): w2.pdf is not there" -- gather refuses
    int                      paper = 0;  // items in by a tick only
};

inline constexpr const char* kContentsName = "00 Contents.txt";

// A name safe on every desktop the receiver might use: / \ : * ? " < > | and
// control characters become a space, runs of spaces fold, dots and spaces are
// trimmed from the ends, and it is cut to 80 bytes (on a UTF-8 boundary).
// Empty in -> "untitled".
std::string safe_file_name(const std::string& s);

// What is gathered, under what names. `when` is the stamp (local date in the
// name and in Contents). Problems are listed rather than thrown: a missing
// file, an item that is not in. The plan of an incomplete packet has problems.
GatherPlan gather_plan(const std::string& title, const PacketState& st, const AttachStore& store,
                       std::int64_t when);

// Into a NEW folder named plan.name inside `parent` ("... (2)" when that name
// is taken). Built as a hidden ".part" folder, then renamed. On success `made`
// is the folder's absolute path.
bool gather_to_folder(const GatherPlan& plan, const std::string& parent, std::string& made,
                      std::string& err);

// Into one zip at `zip_path` (overwritten -- the save dialog asked), the files
// inside a top folder named plan.name. STORED, not deflated: what a packet
// carries is PDFs and pictures, already compressed, and a stored zip needs no
// library. Through a temp file and a rename.
bool gather_to_zip(const GatherPlan& plan, const std::string& zip_path, std::string& err);

// ── the zip writer, exposed for the selftest ────────────────────────────────
std::uint32_t crc32(const std::string& bytes);

struct ZipEntry {
    std::string name;    // path inside the zip, '/' separated
    std::string bytes;
};
// A complete stored zip (local headers, central directory, end record), names
// flagged UTF-8, times from `when` (local). Entries over 4 GB are not ours.
std::string zip_store(const std::vector<ZipEntry>& entries, std::int64_t when);

// "Sent 6 Oct 2026 · Taxes 2026 - 2026-10-06.zip" for Note details.
std::string sent_line(std::int64_t when, const std::string& to);

}  // namespace jot::core
