#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Enclosures -- the things a note CARRIES rather than says (s016b).
//
// The direction (Scott, s016): the drawer grows an enclosure list after
// QuarkXPress's Usage / Affinity's Resource Manager -- per item, embedded or
// linked, a status, metadata, export actions -- built one kind at a time.
// Images are the first kind, and embedded is the first mode.
//
// THE MARKDOWN IS THE TRUTH, the metadata is joined on. A note says
// `![label](attachments/name.png)` and nothing else; the list the drawer shows
// is DERIVED from a scan of the body, and whatever jot remembers about each
// file (where it came from, how big, when) is looked up by name afterwards.
// So an image reference typed by hand shows up, a deleted reference drops out,
// and there is no second list that can disagree with the note.
//
// Two stores, one shape. A jots folder keeps its files in `attachments/` and
// its metadata in jot.json. The scratch buffer keeps its files in
// `~/.local/share/jot/scratch-attachments/` and its metadata in memory -- the
// scratch NOTES are in memory too, so the two are lost or kept together -- and
// Project::adopt() carries both across when the buffer gets a home.
//
// GTK-free, like everything under core/. The UI hands it paths and bytes.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// The folder inside a jots folder, and the prefix a reference is written with.
inline constexpr const char* kAttachDir    = "attachments";
inline constexpr const char* kAttachPrefix = "attachments/";

// What jot remembers about one enclosure. Everything here is REPORT: losing it
// costs a line in the drawer, never the image.
struct EnclosureMeta {
    std::string  mode = "embedded";   // "embedded" only, for now; "linked" is later
    std::string  source;              // the original path, or "clipboard"
    std::int64_t size  = 0;           // bytes, as copied
    std::int64_t added = 0;           // unix seconds
};

// Keyed by filename WITHIN the attachments folder ("photo.png"), never by path:
// the folder moves with its jots and a stored path would go stale.
using EnclosureMetas = std::map<std::string, EnclosureMeta>;

// Where one store's files live, and what it remembers about them.
struct AttachStore {
    std::string    dir;     // absolute; may not exist yet (created on first ingest)
    EnclosureMetas metas;
};

// ── naming ──────────────────────────────────────────────────────────────────

// The original name, slugged: lower case, [a-z0-9] kept, every other run turned
// into ONE dash, trimmed; the extension kept and lower-cased. An empty stem
// becomes "image". "My Photo (1).JPG" -> "my-photo-1.jpg". Slugged so the
// markdown target never needs escaping -- a space or a paren in a filename is
// exactly what breaks `![](...)` in every other editor.
std::string slug_filename(const std::string& original);

// A pasted image has no name, so it gets the moment: "pasted-20260925-171204.png".
// LOCAL time, because it is read by a person looking for "the one from this
// afternoon", not by a machine.
std::string paste_filename(std::int64_t unix_time, const std::string& ext = "png");

// `name` if nothing in `dir` has it, else `stem-2.ext`, `stem-3.ext`... The
// first free one. A directory that does not exist has everything free.
std::string unique_filename(const std::string& dir, const std::string& name);

// Is this a file jot shows as an image? By extension, case-insensitive. The
// drawer asks the image loader for the truth; this is the cheap gate on a drop.
bool is_image_filename(const std::string& name);

// The attachment name a markdown target points at, or EMPTY if it is not one
// of ours: "attachments/a.png" -> "a.png". Refuses anything with a further
// slash or a "..", so a target can never name a file outside the folder.
std::string attachment_name(const std::string& target);

// The reference to write into a note: `![label](attachments/name)`. Brackets
// in the label are dropped rather than escaped -- the label is the original
// filename's stem, and a bracket there is noise, not meaning.
std::string image_markdown(const std::string& label, const std::string& name);

// The label for a file: its stem, as the user named it (not slugged).
std::string image_label(const std::string& original);

// ── ingest -- a file or some bytes become an enclosure ──────────────────────
// Copies into `store.dir` (creating it) under a unique slugged name, records
// the metadata, and returns the NAME (not the path). Empty on failure, with
// `err` saying why. Nothing in the source is touched.
std::string ingest_file(AttachStore& store, const std::string& src_path,
                        std::int64_t now, std::string& err);

// The same for bytes with no file behind them (a clipboard image). `name` is
// the wanted filename, already chosen (paste_filename); it is still made unique.
std::string ingest_bytes(AttachStore& store, const std::string& bytes,
                         const std::string& name, const std::string& source,
                         std::int64_t now, std::string& err);

// ── the derived list ────────────────────────────────────────────────────────
struct Enclosure {
    std::string   name;          // within attachments/
    std::string   label;         // the first reference's label
    int           refs = 0;      // how many times this body points at it
    int           line = 0;      // the first reference's buffer line
    bool          present = false;   // the file exists in the store's dir
    bool          has_meta = false;
    EnclosureMeta meta;
};

// Every `![..](attachments/..)` in `body`, one entry per FILE in first-mention
// order, joined with what the store knows. Plain links to an attachment count
// too -- they are still enclosures -- but http images do not: they are not
// carried, so they are not ours to list.
std::vector<Enclosure> enclosures(const std::string& body, const AttachStore& store);

// Just the names, for callers that only need to know what is referenced.
std::vector<std::string> referenced_attachments(const std::string& body);

// Rewrite every reference to attachments/<from> in `body` as attachments/<to>.
// Returns how many were rewritten. Used by adopt when a name collides at the
// destination, so the note and the file never disagree about the name.
int rename_references(std::string& body, const std::string& from, const std::string& to);

// ── carry -- the scratch buffer (or another folder) into a jots folder ──────
// For each name in `names`: find it in `from.dir`, give it a free name in
// `to.dir`, move (or copy) it there, carry its metadata. Returns the renames
// that happened (old -> new), so the caller rewrites bodies to match. A file
// missing at the source is skipped and left referenced: the note keeps saying
// what it said, and the drawer will say Missing, which is the truth.
//
// `move` true for the scratch buffer (the files are going home), false for a
// Save As from a jots folder (the original folder keeps its own). A move across
// filesystems copies, verifies the size, then removes -- never remove first.
std::map<std::string, std::string> carry_attachments(const AttachStore& from, AttachStore& to,
                                                     const std::vector<std::string>& names,
                                                     bool move, int* carried = nullptr);

// ── out (s017) ──────────────────────────────────────────────────────────────
// The absolute path of an enclosure in a store, or EMPTY if the name is not
// one of ours (a slash, a "..") -- the one place a name becomes a path, so an
// action can never be pointed outside the folder by a crafted reference.
std::string enclosure_path(const AttachStore& store, const std::string& name);

// "Save a copy..." -- the enclosure's bytes to `dest`, which the user chose.
// Through a temp file and a rename, so a failed copy never leaves a half
// file where a good one was. OVERWRITES: the save dialog already asked. A copy
// onto the enclosure itself is refused (it would truncate the only copy).
// Returns true, or false with `err` saying why. The store is never changed.
bool copy_out(const AttachStore& store, const std::string& name, const std::string& dest,
              std::string& err);

// ── the jot.json half ───────────────────────────────────────────────────────
// Encode/decode adjacent (CANON: pumps). JSON text in and out rather than a
// json object, so this header stays free of nlohmann.
std::string    encode_metas(const EnclosureMetas& m);      // a JSON object, "{}" when empty
EnclosureMetas decode_metas(const std::string& json_object);


}  // namespace jot::core
