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
    std::string  mode = "embedded";   // "embedded" or "linked" (s019)
    std::string  source;              // the original path, or "clipboard"
    std::int64_t size  = 0;           // bytes, as copied / as linked
    std::int64_t added = 0;           // unix seconds
    std::int64_t mtime = 0;           // linked: the file's mtime when linked (or accepted)
};

// Keyed by filename WITHIN the attachments folder ("photo.png") for an embedded
// file -- never by path: the folder moves with its jots and a stored path
// would go stale. A LINKED file (s019) is keyed by its canonical file:// URI,
// because for a link the absolute path IS the identity: it lives outside the
// folder and does not move with it.
using EnclosureMetas = std::map<std::string, EnclosureMeta>;

// Where one store's files live, and what it remembers about them.
struct AttachStore {
    std::string    dir;     // absolute; may not exist yet (created on first ingest)
    EnclosureMetas metas;
};

// ── naming ──────────────────────────────────────────────────────────────────

// The original name, slugged: lower case, [a-z0-9] kept, every other run turned
// into ONE dash, trimmed; the extension kept and lower-cased. An empty stem
// becomes "image" for an image and "file" for anything else (s018).
// "My Photo (1).JPG" -> "my-photo-1.jpg". Slugged so the
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
// drawer asks the image loader for the truth. Since s018 this is no longer the
// gate on a drop -- any file is an enclosure -- it is the KIND switch: an image
// is referenced with `![..]`, gets a thumbnail and a Copy Image; anything else
// is a plain `[..]` link with its type's icon.
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

// The reference for ANY enclosure (s018): `![label](attachments/name)` for an
// image, `[label](attachments/name)` for anything else. A LINKED key (s019)
// is written as it is -- `[label](file:///..)` -- with the same kind switch. The bang is what makes
// a viewer inline it; a PDF inlined is a broken-image icon in every renderer.
std::string enclosure_markdown(const std::string& label, const std::string& name);

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

// ── linked (s019) -- a file jot points at where it lives ────────────────────
// A linked enclosure is written as a real link to the file:
// `[Q3 Report](file:///home/scott/Documents/q3%20report.pdf)`. The markdown is
// still the truth, and any other viewer can follow it. Nothing is copied.

// `/home/a b/x.pdf` -> `file:///home/a%20b/x.pdf`. Everything but the RFC 3986
// unreserved characters and `/` is percent-encoded -- including the parens,
// which would end a markdown target. Empty for a path that is not absolute.
std::string file_uri(const std::string& abs_path);

// The absolute path a `file://` target names, decoded, or EMPTY if the target
// is not a local file URI (`file:///p` and `file://localhost/p` only; a NUL
// or a relative path is refused).
std::string linked_path(const std::string& target);

// The canonical key of a linked target: file_uri(linked_path(t)). Two
// spellings of one path (`%20` vs a raw space) are one enclosure. Empty if
// the target is not a local file URI.
std::string linked_key(const std::string& target);

// Is this key (a metas key, an action target) a LINKED enclosure's?
bool is_linked_key(const std::string& key);

// Link a file: refuse a folder, record size + mtime (for Modified), and
// return the KEY (the URI) to write. Empty on failure, with `err` saying why.
// Nothing is copied; the store's dir is not touched or created.
std::string link_file(AttachStore& store, const std::string& abs_path, std::int64_t now,
                      std::string& err);

// The file's size and mtime now. False if it is not a regular file.
bool file_stamp(const std::string& path, std::int64_t& size, std::int64_t& mtime);

// "Accept Change" -- the file is Modified and that is fine: record its current
// size and mtime so it reads OK again. False (with err) if it is not there.
bool accept_change(AttachStore& store, const std::string& key, std::string& err);

// "Relink..." -- `key` now lives at `new_path`. Records the new key's metadata
// (the old key's `added` is kept) and returns the NEW key; the caller rewrites
// the references (retarget_references) in every body that has them. The old
// key's metadata is left alone -- nothing in the store is ever deleted, and
// another folder's note may still point there.
std::string relink(AttachStore& store, const std::string& key, const std::string& new_path,
                   std::int64_t now, std::string& err);

// ── convert (s020) -- change how a note carries a file, after the drop ──────
// The mode stops being a decision made once at drop time. Both verbs return
// the NEW key; the caller rewrites the references in the note (retarget_
// references). Nothing is ever deleted: a linked file stays where it is, and
// an embedded copy stays in attachments/ -- another note may still use it.

// "Embed a Copy" -- a linked key becomes an attachment: the file is copied in
// (ingest_file, so `source` records where it came from). Refuses a key that
// is not linked, and a linked file that is not there.
std::string embed_copy(AttachStore& store, const std::string& key, std::int64_t now,
                       std::string& err);

// The absolute path an EMBEDDED file was copied from, or empty: a paste, a
// hand-placed file, a linked key, no record. Says nothing about whether the
// path still exists -- the caller asks, because the answer changes the verb
// (link straight to it, or choose).
std::string original_of(const AttachStore& store, const std::string& name);

// "Link Instead" -- an embedded name's references will point at `abs_path`
// (usually original_of(name)). Records the link like link_file. Refuses a
// name that is already linked. The embedded copy is left alone.
std::string link_instead(AttachStore& store, const std::string& name, const std::string& abs_path,
                         std::int64_t now, std::string& err);

// Rewrite every reference whose target is `from_key` (an attachment name, or
// a linked key -- any spelling of it) so it points at `to_key` instead.
// Returns how many were rewritten. Labels and bangs are untouched.
int retarget_references(std::string& body, const std::string& from_key, const std::string& to_key);

// Every linked key `body` references, first-mention order, no repeats.
std::vector<std::string> linked_references(const std::string& body);

// ── the derived list ────────────────────────────────────────────────────────
enum class EnclosureStatus { Ok, Missing, Modified };

struct Enclosure {
    std::string   name;          // the KEY: a name within attachments/, or a linked file:// URI
    std::string   label;         // the first reference's label
    std::string   path;          // absolute; where the bytes are (or would be)
    bool          linked = false;    // s019: points at a file outside the folder
    int           refs = 0;      // how many times this body points at it
    int           line = 0;      // the first reference's buffer line
    bool          present = false;   // the file exists
    EnclosureStatus status = EnclosureStatus::Missing;
    bool          has_meta = false;
    EnclosureMeta meta;

    // What the row calls it: the name for an embedded file, the file's own
    // basename for a linked one.
    std::string display() const;
};

// Every `![..](attachments/..)` and every `[..](file:///..)` in `body`, one
// entry per FILE in first-mention order, joined with what the store knows.
// Plain links to an attachment count too -- they are still enclosures -- but
// http images do not: they are not carried, so they are not ours to list.
// A linked file is Modified when its size or mtime differ from what was
// recorded; with no record it is simply OK (a typed-in link has no baseline).
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
// s019: a LINKED key resolves to the path it names, wherever that is -- a
// link points outside the folder by definition, and the user wrote it.
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
