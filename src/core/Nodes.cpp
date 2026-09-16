#include "core/Nodes.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

// core/Nodes.cpp -- the model. Every verb here is a model verb: no widget ever
// moves, copies or deletes a node itself, it asks the source to. That is the
// whole lesson Notr's drag handler teaches, enforced by there being nowhere
// else to do it.

namespace jot::core {

namespace {

std::int64_t now_seconds() { return static_cast<std::int64_t>(std::time(nullptr)); }

std::string make_id(int n) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "n%04d", n);
    return buf;
}

// Highest numeric suffix in a set, so reset() can't mint an id that collides
// with one it just loaded.
int high_water(const std::vector<Node>& nodes) {
    int hi = 0;
    for (const auto& n : nodes) {
        if (n.id.size() < 2 || n.id[0] != 'n') continue;
        int v = 0;
        bool digits = true;
        for (std::size_t i = 1; i < n.id.size(); ++i) {
            if (n.id[i] < '0' || n.id[i] > '9') { digits = false; break; }
            v = v * 10 + (n.id[i] - '0');
        }
        if (digits) hi = std::max(hi, v);
    }
    return hi;
}

}  // namespace

// ── Projections ─────────────────────────────────────────────────────────────

bool is_descendant(const NodeSource& src, const NodeId& id, const NodeId& ancestor) {
    if (ancestor.empty()) return true;          // everything is under the root scope
    if (id.empty()) return false;
    NodeId cur = id;
    // Bounded by count() so a corrupt parent cycle can't hang the UI thread.
    for (std::size_t guard = 0; guard <= src.count(); ++guard) {
        if (cur == ancestor) return true;
        const Node* n = src.find(cur);
        if (!n || n->parent_id.empty()) return false;
        cur = n->parent_id;
    }
    return false;
}

bool can_move(const NodeSource& src, const NodeId& id, const NodeId& new_parent,
              std::string* why) {
    auto no = [&](const char* reason) { if (why) *why = reason; return false; };

    const Node* n = src.find(id);
    if (!n) return no("no such node");
    if (n->protect) return no("node is protected");
    if (!new_parent.empty() && !src.find(new_parent)) return no("no such parent");
    if (id == new_parent) return no("cannot parent a node to itself");
    if (is_descendant(src, new_parent, id)) return no("cannot move a node into its own subtree");
    if (why) why->clear();
    return true;
}

int sibling_index(const NodeSource& src, const NodeId& id) {
    const Node* n = src.find(id);
    if (!n) return -1;
    const auto sibs = src.children(n->parent_id);
    for (std::size_t i = 0; i < sibs.size(); ++i)
        if (sibs[i] == id) return static_cast<int>(i);
    return -1;
}

std::string dump(const NodeSource& src) {
    std::string out = "node tree (" + std::to_string(src.count()) + " nodes)\n";
    // Iterative walk so a deep tree can't blow the stack in a diagnostic.
    struct Frame { NodeId id; int depth; };
    std::vector<Frame> stack;
    const auto roots = src.children("");
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back({*it, 0});
    while (!stack.empty()) {
        const Frame f = stack.back();
        stack.pop_back();
        const Node* n = src.find(f.id);
        if (!n) continue;
        out.append(static_cast<std::size_t>(f.depth) * 2, ' ');
        out += n->id;
        out += "  ";
        out += n->title.empty() ? "(untitled)" : n->title;
        if (n->protect) out += "  [protected]";
        out += "\n";
        const auto kids = src.children(f.id);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
            stack.push_back({*it, f.depth + 1});
    }
    return out;
}

// ── MemoryNodes ─────────────────────────────────────────────────────────────

std::vector<NodeId> MemoryNodes::children(const NodeId& parent) const {
    auto it = m_kids.find(parent);
    return it == m_kids.end() ? std::vector<NodeId>{} : it->second;
}

const Node* MemoryNodes::find(const NodeId& id) const {
    auto it = m_by_id.find(id);
    return it == m_by_id.end() ? nullptr : &m_nodes[it->second];
}

Node* MemoryNodes::mutable_find(const NodeId& id) {
    auto it = m_by_id.find(id);
    return it == m_by_id.end() ? nullptr : &m_nodes[it->second];
}

std::size_t MemoryNodes::count() const { return m_nodes.size(); }

void MemoryNodes::reindex() {
    m_by_id.clear();
    m_kids.clear();
    m_by_id.reserve(m_nodes.size());
    for (std::size_t i = 0; i < m_nodes.size(); ++i) m_by_id[m_nodes[i].id] = i;
    // Sibling order == storage order. Orphans (a parent that isn't here) would
    // vanish from the projection, so they are re-homed at the root rather than
    // silently lost -- a dead parent link is a bug we want visible, not quiet.
    for (auto& n : m_nodes) {
        if (!n.parent_id.empty() && !m_by_id.count(n.parent_id)) n.parent_id.clear();
        m_kids[n.parent_id].push_back(n.id);
    }
}

void MemoryNodes::reset(std::vector<Node> nodes) {
    m_nodes = std::move(nodes);
    reindex();
    m_next = high_water(m_nodes) + 1;
    notify(Change::Reload, "");
}

NodeId MemoryNodes::mint_id() { return make_id(m_next++); }

NodeId MemoryNodes::create(const NodeId& parent, const std::string& title) {
    if (!parent.empty() && !m_by_id.count(parent)) return {};
    Node n;
    n.id        = mint_id();
    n.parent_id = parent;
    n.title     = title;
    n.created   = now_seconds();
    n.modified  = n.created;
    const NodeId id = n.id;
    m_by_id[id] = m_nodes.size();
    m_nodes.push_back(std::move(n));
    m_kids[parent].push_back(id);
    notify(Change::Created, id);
    return id;
}

bool MemoryNodes::set_title(const NodeId& id, const std::string& title) {
    Node* n = mutable_find(id);
    if (!n || n->protect || n->title == title) return false;
    n->title    = title;
    n->modified = now_seconds();
    notify(Change::Title, id);
    return true;
}

bool MemoryNodes::set_body(const NodeId& id, const std::string& body) {
    Node* n = mutable_find(id);
    if (!n || n->protect || n->body == body) return false;
    n->body     = body;
    n->modified = now_seconds();
    notify(Change::Body, id);
    return true;
}

bool MemoryNodes::set_protect(const NodeId& id, bool on) {
    Node* n = mutable_find(id);
    if (!n || n->protect == on) return false;
    n->protect  = on;
    n->modified = now_seconds();
    notify(Change::Flags, id);
    return true;
}

bool MemoryNodes::set_task(const NodeId& id, const Task& t) {
    Node* n = mutable_find(id);
    if (!n || n->task == t) return false;   // no-op writes must not dirty a note
    n->task     = t;
    n->modified = now_seconds();
    notify(Change::Task, id);
    return true;
}

// ── the convenience writers ─────────────────────────────────────────────────
// Read-modify-write through the one virtual. Each is what a checkbox in the
// drawer actually does, and none of them is a second path to the store.
namespace {
bool edit_task(NodeSource& src, const NodeId& id, const std::function<void(Task&)>& fn) {
    const Node* n = src.find(id);
    if (!n) return false;
    Task t = n->task;
    fn(t);
    return src.set_task(id, t);
}
}  // namespace

bool NodeSource::set_done(const NodeId& id, bool on) {
    return edit_task(*this, id, [&](Task& t) { t.done = on; });
}
bool NodeSource::set_flagged(const NodeId& id, bool on) {
    return edit_task(*this, id, [&](Task& t) { t.flagged = on; });
}
bool NodeSource::set_due(const NodeId& id, std::int64_t when) {
    return edit_task(*this, id, [&](Task& t) { t.due = when; });
}
bool NodeSource::set_defer(const NodeId& id, std::int64_t when) {
    return edit_task(*this, id, [&](Task& t) { t.defer = when; });
}
bool NodeSource::set_status(const NodeId& id, Status s) {
    return edit_task(*this, id, [&](Task& t) { t.status = s; });
}
bool NodeSource::make_task(const NodeId& id, bool on) {
    return edit_task(*this, id, [&](Task& t) {
        if (on) { t.is_task = true; return; }
        // Clear everything EXCEPT status: status describes the children, and
        // a note that stops being a todo is still their container.
        const Status keep = t.status;
        t = Task{};
        t.status = keep;
    });
}

// THE move. One field write plus an index fixup; the subtree is untouched
// because it only ever referenced its parent, and the id is not reissued.
// `index` places it among its new siblings (-1 appends). A same-parent move is
// a reorder, which is why the old position is removed BEFORE the index is
// interpreted -- otherwise "put it where row 3 is" lands one short whenever the
// node came from above row 3.
bool MemoryNodes::move(const NodeId& id, const NodeId& new_parent, int index) {
    if (!can_move(*this, id, new_parent, nullptr)) return false;
    Node* n = mutable_find(id);
    const NodeId old_parent = n->parent_id;
    const int    old_index  = sibling_index(*this, id);

    auto& old_siblings = m_kids[old_parent];
    old_siblings.erase(std::remove(old_siblings.begin(), old_siblings.end(), id),
                       old_siblings.end());

    auto& new_siblings = m_kids[new_parent];
    if (old_parent == new_parent && index > old_index) --index;
    const int at = (index < 0 || index > static_cast<int>(new_siblings.size()))
                       ? static_cast<int>(new_siblings.size())
                       : index;

    if (old_parent == new_parent && at == old_index) {
        // Nothing actually changed. Put it back and report no-op rather than
        // notifying, so a dropped-where-it-already-was doesn't repaint.
        new_siblings.insert(new_siblings.begin() + at, id);
        return false;
    }

    n->parent_id = new_parent;
    n->modified  = now_seconds();
    new_siblings.insert(new_siblings.begin() + at, id);
    notify(Change::Moved, id);
    return true;
}

void MemoryNodes::collect_subtree(const NodeId& id, std::vector<NodeId>& out) const {
    out.push_back(id);
    auto it = m_kids.find(id);
    if (it == m_kids.end()) return;
    for (const auto& kid : it->second) collect_subtree(kid, out);
}

bool MemoryNodes::remove(const NodeId& id) {
    const Node* n = find(id);
    if (!n || n->protect) return false;
    std::vector<NodeId> doomed;
    collect_subtree(id, doomed);
    // A protected node anywhere under the cut vetoes the whole delete -- a lock
    // that a parent delete can route around is not a lock.
    for (const auto& d : doomed)
        if (const Node* dn = find(d); dn && dn->protect && d != id) return false;
    std::erase_if(m_nodes, [&](const Node& x) {
        return std::find(doomed.begin(), doomed.end(), x.id) != doomed.end();
    });
    reindex();
    notify(Change::Removed, id);
    return true;
}

// ── capture ─────────────────────────────────────────────────────────────────

namespace {
constexpr std::size_t kTitleMax = 72;

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}  // namespace

void capture_split(const std::string& text, std::string& title, std::string& body) {
    title.clear();
    body.clear();
    const std::string t = trim(text);
    if (t.empty()) return;

    const auto nl = t.find('\n');
    if (nl != std::string::npos) {
        title = trim(t.substr(0, nl));
        body  = trim(t.substr(nl + 1));
        return;
    }

    if (t.size() <= kTitleMax) { title = t; return; }

    // Cut at the last space at or before the limit, so a title ends on a word.
    // If there is no space at all (a pasted url, a long identifier) the hard cut
    // is the honest answer -- and the body still has the whole thing.
    std::size_t cut = t.rfind(' ', kTitleMax);
    if (cut == std::string::npos || cut < kTitleMax / 2) cut = kTitleMax;
    title = trim(t.substr(0, cut)) + "\u2026";
    body  = t;
}

NodeId capture(NodeSource& src, const std::string& text) {
    std::string title, body;
    capture_split(text, title, body);
    if (title.empty()) return {};          // nothing typed is not a note
    const NodeId id = src.create("", title);
    if (!id.empty() && !body.empty()) src.set_body(id, body);
    return id;
}

// ── Fixtures ────────────────────────────────────────────────────────────────
// Hardcoded, arriving through the same NodeSource the store will implement.
// Shaped to put every open question on screen at once: bodies with checkbox
// lines (D2), a jot: link (D4/links), a protected node, nesting past two deep.

std::vector<Node> fixture_nodes() {
    const std::int64_t t = now_seconds();
    auto n = [t](const char* id, const char* parent, const char* title,
                 const char* body, bool prot = false) {
        Node x;
        x.id = id; x.parent_id = parent; x.title = title; x.body = body;
        x.created = t; x.modified = t; x.protect = prot;
        return x;
    };

    return {
        n("n0001", "", "Inbox",
          "Capture lands here. Filing is the second act.\n\n"
          "- [ ] ring the bindery about board stock\n"
          "- [ ] look up whether Folio's editor lifts out\n"),
        n("n0002", "n0001", "thought re: import",
          "The .ntr reader is a one-way importer. Read Notr's records, write\n"
          "jot's own shape, never adopt the format.\n"),

        n("n0003", "", "Projects",
          "Every node parents children, so a project is just a note with notes\n"
          "under it. No folder type.\n"),
        n("n0004", "n0003", "jot",
          "A scribble pad that files itself.\n\n"
          "- [x] spine transplanted from Cairn\n"
          "- [ ] persistence behind the same seam\n"),
        n("n0005", "n0004", "s002 - fixture surface",
          "Tree, editor and DnD on hardcoded fixtures, read through the\n"
          "interface the real store will implement.\n\n"
          "- [ ] drag a subtree and confirm the ids survive\n"
          "- [ ] decide Pole A vs Pole B by measuring, not arguing\n"
          "\nRelated: [the model sketch](jot:n0009)\n"),
        n("n0006", "n0004", "backlog",
          "- [ ] backlink index\n"
          "- [ ] structural undo: delete, move, protect\n"
          "- [ ] search across every note\n"),
        n("n0007", "n0003", "bindery",
          "- [ ] cut signatures for the octavo\n"
          "- [ ] order linen thread\n"),

        n("n0008", "", "Reference",
          "Things that are looked up, not done.\n"),
        n("n0009", "n0008", "the model, in one screen",
          "A node is the only entity. The tree is a projection of a flat set,\n"
          "reconstructed by parent lookup.\n\n"
          "A move is one field write: parent_id. Children come along because\n"
          "they never referenced anything but their parent.\n\n"
          "Links target id, never title or path.\n",
          /*prot=*/true),
        n("n0010", "n0008", "gtk4 notes",
          "Drop target wants the same GType the drag source provides.\n"),

        n("n0011", "", "Someday",
          "- [ ] letterpress the colophon\n"
          "- [ ] a proper .ntr importer\n"),
    };
}

std::vector<Node> stress_nodes(int count) {
    std::vector<Node> out;
    if (count <= 0) return out;
    out.reserve(static_cast<std::size_t>(count));
    const std::int64_t t = now_seconds();
    constexpr int kRoots = 8, kFanout = 4;
    for (int i = 0; i < count; ++i) {
        Node x;
        x.id = make_id(i + 1);
        if (i >= kRoots) x.parent_id = make_id((i - kRoots) / kFanout + 1);
        x.title    = "Note " + std::to_string(i + 1);
        x.body     = "Synthetic node " + std::to_string(i + 1) +
                     ".\n\n- [ ] a task line, so the harvest has something to find\n";
        x.created  = t;
        x.modified = t;
        out.push_back(std::move(x));
    }
    return out;
}

}  // namespace jot::core
