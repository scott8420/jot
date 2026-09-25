#pragma once
#include "core/Enclosures.hpp"
#include "core/Nodes.hpp"

#include <set>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// core/Project -- persistence, behind the SAME interface the fixtures used.
//
// This class is the test s002 was built to run. If wiring it in required
// changing TreePane or EditorPane by a line, the seam was drawn in the wrong
// place (CANON: "fixture data through the real seam"). The only line above the
// seam that changes is the one in Shell that constructs it.
//
// On disk
// -------
//     MyProject/
//       jot.json           the project file -- tree, sibling order, metadata
//       notes/<uuid>.md    one file per note: front-matter id, then the body
//       attachments/       media, referenced relatively from the markdown
//
// **The project file owns the structure.** A reparent is one write to
// jot.json and NOTHING ON DISK MOVES -- no rename, no mv of a subtree, no path
// to get stale. That is the Notr failure mode made unreachable rather than
// merely avoided: there is no filesystem operation for a drag to get wrong.
//
// **Each note carries its own id anyway**, in front-matter. Not for
// interchange -- the jots folder is jot's private store and human-readable output is
// export's job. It is for recovery: if jot.json is lost or corrupted, the ids
// mean a rebuild returns every note as a top-level orphan instead of returning
// a folder of anonymous uuid-named files. The difference is losing the tree
// versus losing everything, and it costs one line per save.
//
// Project IS a MemoryNodes: the model rules -- cycles, protection, sibling index,
// the move -- are already right there and must not be reimplemented. Every
// override does the same two things: let the base change the model, then record
// what that made dirty.
//
// Writes are deferred, not skipped: a structural change writes jot.json
// immediately (it is small and it is the thing you cannot rebuild), while body
// edits mark their file dirty and land on flush(), because a keystroke is not
// a reason to touch the disk. The CALLER drives flush -- the UI owns the timer,
// as it owns the XDG path resolution, and this file stays GTK-free.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

// ── the .jots naming convention ─────────────────────────────────────────────
// A jots folder is named `<name>.jots`. The suffix makes the folder
// self-identifying on disk and in a file manager, and it gives the header a
// real name to show instead of the word "jots" repeated on every machine.
//
// It is a convention, not a requirement: is_jots_dir() still recognises a
// folder by its CONTENTS, so a folder made by hand or by an earlier version
// opens fine and simply shows its whole name.

inline constexpr const char* kJotsSuffix = ".jots";

// The name to show for a jots folder: its directory name with `.jots` removed.
// "/home/scott/Documents/Field notes.jots" -> "Field notes".
std::string jots_display_name(const std::string& dir);

// The directory name for a chosen display name: `<name>.jots`, or EMPTY if the
// name can't be one -- blank, or carrying a path separator. Returning empty
// rather than sanitising silently is deliberate: the UI greys its button, so
// the user sees the refusal before they commit to it rather than discovering
// afterwards that their name was rewritten.
std::string jots_folder_name(const std::string& name);

// A fresh uuid, in the usual 8-4-4-4-12 hex shape. Filenames never change, so
// this is minted exactly once per note in its whole life.
std::string make_uuid();

// ── looking at a folder without opening it ──────────────────────────────────
// The first-run question ("is there already something at the old default?")
// and the relocate guard ("is the destination free?") both need to inspect a
// directory without constructing a Project over it. Free functions rather than
// statics because neither one needs a store, and both want to be answerable in
// the selftest.

// Does this directory already hold jots? True if it has a jot.json or any
// notes/*.md -- either one alone is enough, because a lost project file is a
// recovery case and not an empty folder.
bool is_jots_dir(const std::string& dir);

// How many notes/<uuid>.md files are in it. 0 for anything that isn't a jots
// folder, so this doubles as "is there anything here worth moving".
std::size_t jots_note_count(const std::string& dir);

// ── relocate ────────────────────────────────────────────────────────────────
// Move a whole jots folder from one place to another.
//
// This is safe SPECIFICALLY because of the shape D1 chose: nothing inside a
// jots folder holds an absolute path, jot.json addresses by id, and every
// filename is a uuid minted once. So a relocate is a rename of one directory
// and nothing inside it has to be rewritten or even read. Notr could not have
// survived this operation.
//
// Returns an error message; EMPTY means it moved. Every refusal happens BEFORE
// anything on disk is touched, and the cross-filesystem fallback verifies the
// copy arrived before it removes the original -- a half-moved jots folder is
// the one outcome this function must never produce.
std::string relocate_jots(const std::string& from, const std::string& to);

class Project : public MemoryNodes {
public:
    ~Project() override;

    // Point at a jots folder directory and read it. Creates the layout if it isn't
    // there. Returns false only if the directory can't be used at all; a
    // missing or unreadable jot.json is a RECOVERY case, not a failure -- see
    // `recovered_orphans()`.
    bool open(const std::string& dir);

    // Write anything outstanding. Safe to call when nothing is dirty.
    bool flush();

    // ── adopt -- give the scratch buffer a home ──────────────────────────────
    // Copy everything in `from` into this (freshly opened, EMPTY) jots folder,
    // preserving the tree, sibling order, titles, bodies, flags and timestamps.
    //
    // This is what "Save" means when jot has been running with no jots folder.
    // The ids cannot come along: a MemoryNodes mints `n0001`, and a Project's
    // ids BECOME FILENAMES, so every node is re-minted as a uuid and every
    // parent reference is remapped through the same map. That remap is the
    // whole risk in this function -- a parent mapped to a stale id is a subtree
    // that silently goes missing -- which is why it lives here, under the
    // selftest, rather than in a dialog's callback.
    //
    // Walks from the roots, so it copies what the TREE holds. Returns the
    // number of nodes adopted; a caller comparing that against from.count()
    // learns whether the source had anything unreachable in it. Refuses (0) if
    // this project already has notes in it.
    //
    // ENCLOSURES (s016b). `att` is where the source's attachments live and
    // what is known about them -- the scratch buffer's staging folder, or the
    // old jots folder on a Save As. Every attachment the adopted bodies
    // REFERENCE is carried into this folder's attachments/ (moved when
    // `move_files`, copied otherwise), metadata with it; a name that collides
    // here gets a free one and the bodies are rewritten to match BEFORE they
    // are written, so a note never points at a name its file does not have.
    // Unreferenced files are left where they are: nothing is deleted.
    std::size_t adopt(const NodeSource& from, const AttachStore* att = nullptr,
                      bool move_files = false);

    // ── enclosures ──────────────────────────────────────────────────────────
    // The attachments/ folder and what jot.json remembers about each file.
    const AttachStore& attach() const { return m_attach; }
    // A new enclosure's metadata. Structure: it lives in jot.json and is
    // written now, not deferred -- the file is already on disk.
    void record_enclosure(const std::string& name, const EnclosureMeta& meta);
    // The ingest path mutates the store directly; this is the same write.
    AttachStore& attach_for_ingest() { return m_attach; }
    void         enclosures_changed() { m_structure_dirty = true; flush(); }

    const std::string& dir() const { return m_dir; }
    bool dirty() const { return m_structure_dirty || !m_dirty_bodies.empty(); }

    // How many notes were adopted from notes/*.md because jot.json didn't
    // mention them. Non-zero means the project file was lost or edited behind
    // our back, and the surface should say so rather than quietly proceeding.
    std::size_t recovered_orphans() const { return m_recovered; }

    // ── the NodeSource writes, each: base first, then mark dirty ────────────
    NodeId create(const NodeId& parent, const std::string& title) override;
    bool   set_title(const NodeId& id, const std::string& title) override;
    bool   set_body(const NodeId& id, const std::string& body) override;
    bool   set_protect(const NodeId& id, bool on) override;
    bool   set_task(const NodeId& id, const Task& t) override;
    using MemoryNodes::move;
    bool   move(const NodeId& id, const NodeId& new_parent, int index) override;
    bool   remove(const NodeId& id) override;

protected:
    NodeId mint_id() override;        // uuids, because the id becomes the filename

private:
    std::string note_path(const NodeId& id) const;
    bool        save_project() const;                 // jot.json
    bool        save_body(const NodeId& id) const;    // notes/<uuid>.md
    bool        load_project(std::vector<Node>& out) const;
    void        load_bodies(std::vector<Node>& nodes) const;
    void        adopt_orphans(std::vector<Node>& nodes);

    std::string        m_dir;
    AttachStore        m_attach;         // dir = <m_dir>/attachments
    std::set<NodeId>   m_dirty_bodies;
    std::set<NodeId>   m_deleted;        // files to unlink on the next flush
    bool               m_structure_dirty = false;
    std::size_t        m_recovered       = 0;
    bool               m_loading         = false;   // suppress dirt during open()
};

}  // namespace jot::core
