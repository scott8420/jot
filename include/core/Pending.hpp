#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Pending -- the spool a capture waits in when jot is NOT RUNNING.
//
// `jot --capture "milk"` with a live jot forwards over the session bus and the
// running instance files it; that has worked since s008 and is still the normal
// path. This is the FALLBACK, and the whole of it is: write the thought down
// where the next launch will find it, and do not open a window to do it.
//
// ── WHY NOT INSIDE THE JOTS FOLDER ──────────────────────────────────────────
// Two reasons, either one sufficient:
//
//   * `jot.json` with two writers is corruption. A cold capture is a second
//     process; the moment it touches the project file it is racing whatever
//     else may start while it writes.
//   * A capture can happen before any jots folder exists. jot opens on a
//     scratch buffer on a fresh machine and asks where notes live on the way
//     OUT -- so "file it in the jots folder" has no answer at 9am on day one.
//
// So the spool lives under the XDG data dir, beside `recent.json` and
// `prefs.json`, and belongs to the APPLICATION rather than to any one set of
// notes.
//
// ── THE ONE RULE ────────────────────────────────────────────────────────────
// A pending file holds the ONLY copy of what was typed. So the deletion is a
// separate call from the read -- `read_pending` never removes anything, and
// `remove_pending` is called only after the note exists somewhere that survives
// the process. A crash mid-drain therefore costs a DUPLICATE, never a loss, and
// that is the correct way round.
//
// ── ON DISK ─────────────────────────────────────────────────────────────────
//   ---
//   jot: capture
//   captured: 1758038602
//   ---
//   buy milk
//
// Front matter because jot's note files already use it, so a spooled capture
// reads like the thing it is about to become, and because a plain text file
// with a date in it is debuggable from a file manager -- which is the whole
// reason every widget in this app is named.
//
// Encode and decode live adjacent, in one file, so a write cannot skew from its
// read (CANON: pumps at conceptual seams). GTK-free: the CALLER resolves the
// XDG dir and hands a plain path in, exactly as Recents and Prefs do.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// One capture waiting on disk.
struct Pending {
    std::string  path;          // the file it came out of -- what remove_pending takes
    std::string  text;          // exactly what was typed, newlines and all
    std::int64_t captured = 0;  // epoch seconds; 0 when the file carried no date
};

// The spool, given the XDG data dir. One definition of the subpath, so App and
// Shell cannot come to disagree about where captures go.
std::string pending_dir(const std::string& data_dir);

// The pump. `when` is epoch seconds; `uniq` distinguishes two captures taken in
// the same second (the caller passes something per-process -- a pid).
std::string encode_pending(const std::string& text, std::int64_t when);
bool        decode_pending(const std::string& raw, Pending& out);
std::string pending_name(std::int64_t when, const std::string& uniq);

// Write one capture into `dir`, creating the directory if it isn't there.
// ATOMIC -- a temp file plus a rename -- so a drain running at the same instant
// reads a whole thought or no file at all, never half of one. Never clobbers:
// a name already taken gets a suffix rather than overwriting someone's note.
// `wrote` takes the path actually used. Returns false if nothing reached disk.
bool write_pending(const std::string& dir, const std::string& text,
                   std::int64_t when, const std::string& uniq,
                   std::string* wrote = nullptr);

// Everything waiting, OLDEST FIRST, so the drained notes land in the order they
// were thought of. Removes nothing. A file with no front matter is read as a
// bare thought rather than skipped -- somebody echoing a line into this folder
// by hand has said something, and dropping it would be the one unforgivable
// behaviour for a capture spool.
std::vector<Pending> read_pending(const std::string& dir);

// Delete one, AFTER its note is somewhere that survives the process.
bool remove_pending(const Pending& p);

}  // namespace jot::core
