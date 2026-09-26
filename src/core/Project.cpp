#include "core/Project.hpp"

#include "json.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_map>
#include <utility>

// src/core/Project.cpp -- the persistence pump. Encode and decode live ADJACENT in
// this one file so a write cannot skew from its read (CANON: pumps at
// conceptual seams, and round-trip fidelity is the bar). The selftest proves
// the round trip on a real temp directory rather than in the abstract.

namespace jot::core {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr const char* kProjectFile = "jot.json";
constexpr const char* kNotesDir    = "notes";
// 2 as of s007: the node objects in jot.json may now carry task fields. A
// version 1 file loads unchanged -- every task field is absent and every node
// is a note, which is exactly what a version 1 folder meant. THE NOTE FILES ON
// DISK DO NOT CHANGE AT ALL; task state is structure, and structure lives here.
//
// 3 as of s016b: an optional top-level "enclosures" object, filename -> what
// jot remembers about it (mode, source, size, added). Absent when there are
// none, and a v2 reader ignores it -- losing it costs report lines in the
// drawer, never an image, because the markdown is what points at the file.
constexpr int         kFormatVersion = 3;

// Status goes to disk as a WORD, not as an integer. An enum's numeric value is
// a fact about this build's declaration order; "sequential" in a file still
// means sequential after somebody adds a case in the middle of the enum.
const char* status_name(Status s) {
    switch (s) {
        case Status::Sequential: return "sequential";
        case Status::Parallel:   return "parallel";
        case Status::None:       break;
    }
    return "none";
}
Status status_from(const std::string& s) {
    if (s == "sequential") return Status::Sequential;
    if (s == "parallel")   return Status::Parallel;
    return Status::None;
}

// Write through a temp file and rename. A rename within one filesystem is
// atomic, so a crash mid-write leaves the previous jot.json intact rather than
// a truncated one -- and jot.json is the file whose loss costs the tree.
bool write_atomic(const fs::path& target, const std::string& content) {
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << content;
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Front-matter is deliberately minimal: the id and nothing else. Everything
// else is the project file's job, and a second home for the title would be a
// second thing to keep in step.
std::string front_matter(const std::string& id) {
    return "---\nid: " + id + "\n---\n";
}

// Split "---\n...\n---\n<body>" into its id and its body. A file with no
// front-matter is not an error -- it yields an empty id and its whole content
// as the body, which is exactly what a hand-dropped markdown file looks like.
void split_front_matter(const std::string& text, std::string& id, std::string& body) {
    id.clear();
    if (text.rfind("---\n", 0) != 0) { body = text; return; }
    const std::size_t end = text.find("\n---\n", 3);
    if (end == std::string::npos) { body = text; return; }
    const std::string head = text.substr(4, end - 3);
    std::istringstream hs(head);
    for (std::string line; std::getline(hs, line);) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.erase(value.begin());
        while (!value.empty() && (value.back() == '\r' || value.back() == ' '))
            value.pop_back();
        if (key == "id") id = value;
    }
    body = text.substr(end + 5);
}

// First non-empty line, trimmed of markdown heading marks -- the only title a
// recovered orphan can offer.
std::string title_from_body(const std::string& body) {
    std::istringstream bs(body);
    for (std::string line; std::getline(bs, line);) {
        while (!line.empty() && (line.front() == '#' || line.front() == ' '))
            line.erase(line.begin());
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty()) return line.substr(0, 80);
    }
    return {};
}

}  // namespace

std::string make_uuid() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> bits;
    const std::uint64_t a = bits(rng), b = bits(rng);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%08x-%04x-4%03x-%04x-%012llx",
                  static_cast<unsigned>(a >> 32),
                  static_cast<unsigned>((a >> 16) & 0xffff),
                  static_cast<unsigned>(a & 0x0fff),
                  static_cast<unsigned>(((b >> 48) & 0x3fff) | 0x8000),
                  static_cast<unsigned long long>(b & 0xffffffffffffULL));
    return buf;
}

// ── the .jots naming convention ─────────────────────────────────────────────

std::string jots_display_name(const std::string& dir) {
    if (dir.empty()) return {};
    std::string name = fs::path(dir).filename().string();
    const std::string suffix = kJotsSuffix;
    if (name.size() > suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        name.erase(name.size() - suffix.size());
    // A folder named exactly ".jots" has no name left; show it as it is on disk
    // rather than as an empty header.
    return name.empty() ? fs::path(dir).filename().string() : name;
}

std::string jots_folder_name(const std::string& name) {
    std::string s = name;
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    if (s.empty()) return {};
    if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos) return {};
    if (s == "." || s == "..") return {};
    // Already carrying the suffix (the user typed it, or pasted a folder name)
    // -- don't double it.
    const std::string suffix = kJotsSuffix;
    if (s.size() > suffix.size() &&
        s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
        return s;
    return s + suffix;
}

// ── looking at a folder without opening it ──────────────────────────────────

std::size_t jots_note_count(const std::string& dir) {
    if (dir.empty()) return 0;
    std::error_code ec;
    const fs::path notes = fs::path(dir) / kNotesDir;
    if (!fs::is_directory(notes, ec)) return 0;
    std::size_t n = 0;
    for (const auto& e : fs::directory_iterator(notes, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec) && e.path().extension() == ".md") ++n;
    }
    return n;
}

bool is_jots_dir(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    if (fs::is_regular_file(fs::path(dir) / kProjectFile, ec)) return true;
    return jots_note_count(dir) > 0;
}

// ── relocate ────────────────────────────────────────────────────────────────

namespace {

// Is `child` inside `parent` (or the same path)? Moving a folder into its own
// subtree is the one destructive mistake available here, and it is lexical
// rather than empirical -- weakly_canonical so `..` and symlinked prefixes
// can't smuggle it past.
bool is_within(const fs::path& child, const fs::path& parent) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(child, ec);
    const fs::path p = fs::weakly_canonical(parent, ec);
    auto ci = c.begin();
    for (auto pi = p.begin(); pi != p.end(); ++pi, ++ci)
        if (ci == c.end() || *ci != *pi) return false;
    return true;
}

bool dir_is_empty(const fs::path& p) {
    std::error_code ec;
    return fs::directory_iterator(p, ec) == fs::directory_iterator();
}

}  // namespace

std::string relocate_jots(const std::string& from, const std::string& to) {
    std::error_code ec;
    const fs::path src = from, dst = to;

    // ── refusals, all of them before anything is touched ────────────────────
    if (from.empty() || to.empty())          return "No folder given.";
    if (!fs::is_directory(src, ec))          return "There is nothing at " + from + ".";
    if (is_within(dst, src))                 return "That would move the folder inside itself.";
    if (fs::exists(dst, ec)) {
        if (!fs::is_directory(dst, ec))      return to + " is a file, not a folder.";
        if (!dir_is_empty(dst))              return "There is already something at " + to + ".";
        // An empty directory at the destination is a chooser artefact (the user
        // made the folder in the file dialog). Clear it so rename has a free
        // name rather than relying on rename-onto-empty-dir semantics.
        fs::remove(dst, ec);
        if (ec)                              return "Cannot clear " + to + ": " + ec.message();
    }
    fs::create_directories(dst.parent_path(), ec);
    if (ec && !fs::is_directory(dst.parent_path()))
        return "Cannot create " + dst.parent_path().string() + ": " + ec.message();

    const std::size_t before = jots_note_count(from);

    // ── the move ────────────────────────────────────────────────────────────
    // Same filesystem: one rename, atomic, nothing inside is read.
    ec.clear();
    fs::rename(src, dst, ec);
    if (!ec) return {};

    // Different filesystem (EXDEV): copy, VERIFY, then remove. The verify is
    // the whole difference between a move and a way to lose notes -- if the
    // copy is short, the original stays and the caller is told.
    if (ec != std::errc::cross_device_link)
        return "Cannot move to " + to + ": " + ec.message();

    ec.clear();
    fs::copy(src, dst, fs::copy_options::recursive, ec);
    if (ec) {
        std::error_code ec2;
        fs::remove_all(dst, ec2);             // leave no half-copy behind
        return "Cannot copy to " + to + ": " + ec.message();
    }
    if (jots_note_count(to) != before) {
        std::error_code ec2;
        fs::remove_all(dst, ec2);
        return "The copy at " + to + " came up short -- nothing was moved.";
    }
    fs::remove_all(src, ec);
    if (ec) return "Copied to " + to + ", but the old folder at " + from +
                   " could not be removed: " + ec.message();
    return {};
}

// ── adopt ───────────────────────────────────────────────────────────────────

std::size_t Project::adopt(const NodeSource& from, const AttachStore* att, bool move_files) {
    if (m_dir.empty() || count() != 0 || from.count() == 0) return 0;

    // PREORDER from the roots, which is also the order jot.json wants: the
    // loader rebuilds each parent's child list in the order it meets them, so
    // preorder in means sibling order out. Iterative rather than recursive
    // because a pasted-in tree's depth is not ours to bound.
    std::vector<std::pair<NodeId, int>> stack;   // id, depth -- depth unused, kept for clarity
    std::vector<NodeId> order;
    {
        const auto roots = from.children("");
        for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back({*it, 0});
        while (!stack.empty()) {
            const auto [cur, d] = stack.back();
            stack.pop_back();
            order.push_back(cur);
            const auto kids = from.children(cur);
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back({*it, d + 1});
        }
    }

    // Every id is re-minted BEFORE any parent is looked up, so a child met
    // before its parent (which preorder makes impossible, but the map does not
    // rely on that) still resolves. The map is the only thing standing between
    // this and a lost subtree.
    std::unordered_map<NodeId, NodeId> remap;
    remap.reserve(order.size());
    for (const auto& old_id : order) remap.emplace(old_id, make_uuid());

    std::vector<Node> adopted;
    adopted.reserve(order.size());
    for (const auto& old_id : order) {
        const Node* src = from.find(old_id);
        if (!src) continue;                       // vanished mid-walk; cannot happen in-process
        Node n = *src;
        n.id = remap[old_id];
        if (!n.parent_id.empty()) {
            const auto it = remap.find(n.parent_id);
            // A parent outside the walk would be a dangling reference. Adopting
            // the node as a ROOT loses its place but never loses the note, and
            // that is the right way round to fail.
            n.parent_id = (it == remap.end()) ? NodeId{} : it->second;
        }
        adopted.push_back(std::move(n));
    }

    // Enclosures BEFORE the bodies are written: a collision here renames the
    // file, and the bodies must say the new name the first time they touch
    // the disk rather than being patched afterwards.
    if (att) {
        std::vector<std::string> names;
        for (const auto& n : adopted)
            for (auto& a : referenced_attachments(n.body))
                if (std::find(names.begin(), names.end(), a) == names.end())
                    names.push_back(std::move(a));
        const auto renames = carry_attachments(*att, m_attach, names, move_files);
        for (const auto& [old_name, new_name] : renames)
            for (auto& n : adopted) rename_references(n.body, old_name, new_name);
        // Linked files (s019) do not move -- only what jot remembers about
        // them does, so Modified still has its baseline in the new folder.
        for (const auto& n : adopted)
            for (const auto& k : linked_references(n.body))
                if (auto m = att->metas.find(k); m != att->metas.end())
                    m_attach.metas.emplace(k, m->second);
    }

    const std::size_t n_adopted = adopted.size();
    std::set<NodeId> bodies;
    for (const auto& n : adopted) bodies.insert(n.id);

    m_loading = true;
    reset(std::move(adopted));       // MemoryNodes indexes it and notifies Reload
    m_loading = false;

    // Everything is new to the disk: every body needs a file and the structure
    // needs writing. reset() does not mark anything dirty -- it is the LOAD
    // path -- so adopt says so explicitly rather than relying on it.
    m_dirty_bodies = std::move(bodies);
    m_structure_dirty = true;
    flush();
    return n_adopted;
}

Project::~Project() {
    // Anything deferred still has to land. A destructor is the last honest
    // moment; the UI should have flushed before this, and if it didn't, losing
    // a body edit to tidy shutdown would be the worst kind of data loss --
    // silent and ours.
    flush();
}

std::string Project::note_path(const NodeId& id) const {
    return (fs::path(m_dir) / kNotesDir / (id + ".md")).string();
}

// ── open ────────────────────────────────────────────────────────────────────

bool Project::open(const std::string& dir) {
    std::error_code ec;
    fs::create_directories(fs::path(dir) / kNotesDir, ec);
    if (ec) return false;
    fs::create_directories(fs::path(dir) / kAttachDir, ec);

    m_dir = dir;
    m_attach = AttachStore{(fs::path(dir) / kAttachDir).string(), {}};
    m_recovered = 0;
    m_dirty_bodies.clear();
    m_deleted.clear();
    m_structure_dirty = false;

    std::vector<Node> nodes;
    load_project(nodes);      // a missing or broken project file is recovery, not failure
    {
        json j = json::parse(read_file(fs::path(m_dir) / kProjectFile), nullptr, false);
        if (!j.is_discarded() && j.is_object() && j.contains("enclosures"))
            m_attach.metas = decode_metas(j["enclosures"].dump());
    }
    load_bodies(nodes);
    adopt_orphans(nodes);

    m_loading = true;
    reset(std::move(nodes));  // MemoryNodes indexes it and notifies Reload
    m_loading = false;

    // A recovered jots folder is a changed jots: write the rebuilt structure back
    // straight away so the next open is clean.
    if (m_recovered > 0) { m_structure_dirty = true; flush(); }
    return true;
}

bool Project::load_project(std::vector<Node>& out) const {
    const std::string text = read_file(fs::path(m_dir) / kProjectFile);
    if (text.empty()) return false;

    json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.contains("nodes") || !j["nodes"].is_array()) return false;

    // Array order IS sibling order. MemoryNodes::reindex() builds each parent's
    // child list in the order it meets them, so preorder in, preorder out.
    for (const auto& e : j["nodes"]) {
        if (!e.is_object() || !e.contains("id")) continue;
        Node n;
        n.id        = e.value("id", std::string{});
        n.parent_id = e.value("parent", std::string{});
        n.title     = e.value("title", std::string{});
        n.created   = e.value("created", std::int64_t{0});
        n.modified  = e.value("modified", std::int64_t{0});
        n.protect   = e.value("protect", false);
        // Task fields, each absent-means-default. Read defensively rather than
        // by schema: a v1 file has none of them, and a hand-edited file may
        // have some of them.
        n.task.is_task = e.value("task", false);
        n.task.done    = e.value("done", false);
        n.task.due     = e.value("due", std::int64_t{0});
        n.task.defer   = e.value("defer", std::int64_t{0});
        n.task.flagged = e.value("flagged", false);
        n.task.status  = status_from(e.value("status", std::string{}));
        if (!n.id.empty()) out.push_back(std::move(n));
    }
    return true;
}

void Project::load_bodies(std::vector<Node>& nodes) const {
    for (auto& n : nodes) {
        const std::string text = read_file(note_path(n.id));
        if (text.empty()) continue;      // a note with no file yet is an empty note
        std::string id_in_file;
        split_front_matter(text, id_in_file, n.body);
    }
}

// The reason the id is written into every file. Any notes/*.md the project
// file doesn't know about is re-adopted at the top level under the id it
// carries -- so a lost jot.json costs the TREE, not the notes.
void Project::adopt_orphans(std::vector<Node>& nodes) {
    std::error_code ec;
    const fs::path notes = fs::path(m_dir) / kNotesDir;
    if (!fs::is_directory(notes, ec)) return;

    std::set<NodeId> known;
    for (const auto& n : nodes) known.insert(n.id);

    std::vector<Node> found;
    for (const auto& entry : fs::directory_iterator(notes, ec)) {
        if (ec) break;
        if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
        std::string id, body;
        split_front_matter(read_file(entry.path()), id, body);
        if (id.empty()) id = entry.path().stem().string();   // trust the filename as a last resort
        if (id.empty() || known.count(id)) continue;
        known.insert(id);
        Node n;
        n.id    = id;
        n.title = title_from_body(body);
        if (n.title.empty()) n.title = "(recovered)";
        n.body  = std::move(body);
        found.push_back(std::move(n));
    }
    // Stable order for a recovery, so two rebuilds of the same folder agree.
    std::sort(found.begin(), found.end(),
              [](const Node& a, const Node& b) { return a.id < b.id; });
    m_recovered = found.size();
    for (auto& n : found) nodes.push_back(std::move(n));
}

// ── save ────────────────────────────────────────────────────────────────────

bool Project::save_project() const {
    json j;
    j["format"]  = "jot-jots folder";
    j["version"] = kFormatVersion;
    j["nodes"]   = json::array();

    // Preorder, so array order carries sibling order. Iterative, because a deep
    // jots folder must not be able to blow the stack on a save.
    std::vector<NodeId> stack;
    const auto roots = children("");
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back(*it);
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (const Node* n = find(id)) {
            json e = {{"id", n->id},
                      {"parent", n->parent_id},
                      {"title", n->title},
                      {"created", n->created},
                      {"modified", n->modified},
                      {"protect", n->protect}};
            // Task fields are written ONLY when they are not the default. A
            // folder of notes then looks exactly as it did under version 1,
            // and a diff of jot.json shows the todos rather than six false
            // flags on every node.
            if (n->task.is_task)               e["task"]    = true;
            if (n->task.done)                  e["done"]    = true;
            if (n->task.due != 0)              e["due"]     = n->task.due;
            if (n->task.defer != 0)            e["defer"]   = n->task.defer;
            if (n->task.flagged)               e["flagged"] = true;
            if (n->task.status != Status::None) e["status"] = status_name(n->task.status);
            j["nodes"].push_back(std::move(e));
        }
        const auto kids = children(id);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    }
    if (!m_attach.metas.empty()) j["enclosures"] = json::parse(encode_metas(m_attach.metas));
    return write_atomic(fs::path(m_dir) / kProjectFile, j.dump(2) + "\n");
}

bool Project::save_body(const NodeId& id) const {
    const Node* n = find(id);
    if (!n) return false;
    return write_atomic(note_path(id), front_matter(id) + n->body);
}

bool Project::flush() {
    if (m_dir.empty()) return false;
    bool ok = true;

    std::error_code ec;
    for (const auto& id : m_deleted) fs::remove(note_path(id), ec);
    m_deleted.clear();

    for (const auto& id : m_dirty_bodies) ok = save_body(id) && ok;
    m_dirty_bodies.clear();

    if (m_structure_dirty) {
        ok = save_project() && ok;
        m_structure_dirty = false;
    }
    return ok;
}

// ── the writes: base first, then record what it made dirty ──────────────────

NodeId Project::mint_id() { return make_uuid(); }

void Project::record_enclosure(const std::string& name, const EnclosureMeta& meta) {
    m_attach.metas[name] = meta;
    m_structure_dirty = true;
    flush();
}

NodeId Project::create(const NodeId& parent, const std::string& title) {
    const NodeId id = MemoryNodes::create(parent, title);
    if (id.empty() || m_loading) return id;
    m_structure_dirty = true;
    m_dirty_bodies.insert(id);   // the file must exist even while it is empty
    flush();                     // a new note is structure; do not defer it
    return id;
}

bool Project::set_title(const NodeId& id, const std::string& title) {
    if (!MemoryNodes::set_title(id, title)) return false;
    m_structure_dirty = true;    // the title lives in jot.json, not in the file
    return true;
}

bool Project::set_body(const NodeId& id, const std::string& body) {
    if (!MemoryNodes::set_body(id, body)) return false;
    m_dirty_bodies.insert(id);   // deferred: a keystroke is not a reason to write
    return true;
}

bool Project::set_protect(const NodeId& id, bool on) {
    if (!MemoryNodes::set_protect(id, on)) return false;
    m_structure_dirty = true;
    flush();
    return true;
}

// Task state is STRUCTURE (D4): it lives in jot.json beside the title and the
// parent, so it takes the structural path -- written immediately, not deferred
// like a body. Ticking a box is the one edit whose loss you would not notice
// until you did the job twice.
bool Project::set_task(const NodeId& id, const Task& t) {
    if (!MemoryNodes::set_task(id, t)) return false;
    m_structure_dirty = true;
    flush();
    return true;
}

// THE move, persisted: one write to jot.json and nothing on disk moves. No
// rename, no mv of a subtree, no stale path. The file for a node keeps its
// name for the whole life of the note.
bool Project::move(const NodeId& id, const NodeId& new_parent, int index) {
    if (!MemoryNodes::move(id, new_parent, index)) return false;
    m_structure_dirty = true;
    flush();
    return true;
}

bool Project::remove(const NodeId& id) {
    // Collect the subtree BEFORE the base forgets it -- afterwards there is
    // nothing left to ask which files to unlink.
    std::vector<NodeId> doomed;
    std::vector<NodeId> stack{id};
    while (!stack.empty()) {
        const NodeId cur = stack.back();
        stack.pop_back();
        doomed.push_back(cur);
        for (const auto& k : children(cur)) stack.push_back(k);
    }

    if (!MemoryNodes::remove(id)) return false;

    for (const auto& d : doomed) {
        m_dirty_bodies.erase(d);
        m_deleted.insert(d);
    }
    m_structure_dirty = true;
    flush();
    return true;
}

}  // namespace jot::core
