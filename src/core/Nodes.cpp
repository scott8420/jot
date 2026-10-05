#include <cctype>
#include "core/Nodes.hpp"
#include "core/Undo.hpp"   // s046b: a capture is ONE undo step, whoever calls it

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
    n.created   = now();          // s037: the injectable clock -- a review counts from it
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

bool MemoryNodes::set_inbox(const NodeId& id, bool on) {
    Node* n = mutable_find(id);
    if (!n || n->inbox == on) return false;   // a no-op must not dirty jot.json
    n->inbox = on;                            // NOT `modified`: the note did not change
    notify(Change::Flags, id);
    return true;
}

bool MemoryNodes::set_packet(const NodeId& id, bool on) {
    Node* n = mutable_find(id);
    if (!n || n->packet == on) return false;
    n->packet = on;
    notify(Change::Flags, id);
    return true;
}

std::int64_t MemoryNodes::now() const { return m_clock ? m_clock() : now_seconds(); }

// s032. How a task record is finished: 0 not, 1 done / completed, 2 dropped.
// A change of KIND (completed -> dropped) is a new event and gets a new time;
// staying finished keeps the time it already had.
namespace {
int finish_kind(const Task& t) {
    if (t.is_task && t.done) return 1;
    if (t.project == ProjectState::Completed) return 1;
    if (t.project == ProjectState::Dropped) return 2;
    return 0;
}
}  // namespace

bool MemoryNodes::set_task(const NodeId& id, const Task& in) {
    Node* n = mutable_find(id);
    if (!n) return false;
    Task t = in;
    const int was = finish_kind(n->task);
    const int is  = finish_kind(t);
    if (is == 0) {
        t.finished = 0;                                        // reopened: no stale time
    } else if (is != was) {
        // Entering a finished state. A caller that copied the old record still
        // carries the old stamp (or 0); one that set a different time on
        // purpose (an import, one day an undo) keeps it.
        if (t.finished == 0 || t.finished == n->task.finished) t.finished = now();
    } else if (t.finished == 0) {
        t.finished = n->task.finished;                         // still finished: keep when
    }
    // s033. Ticking a REPEATING todo finishes this occurrence, not the note:
    // the occurrence goes to the history (the Logbook reads it), and the note
    // rolls on -- dates forward, tick off. Here, at the one door, so every
    // road to done (tree, Today, drawer, project menu) repeats alike.
    bool rolled = false;
    if (t.is_task && t.repeat.on() && is == 1 && was != 1) {
        const std::int64_t at = now();
        m_history.push_back(LogRecord{id, n->title, at});
        repeat_next(t.repeat, at, t.due, t.defer);
        t.done     = false;
        t.finished = 0;
        if (t.project == ProjectState::Completed) t.project = ProjectState::Active;
        rolled = true;
    }
    if (n->task == t && !rolled) return false;   // no-op writes must not dirty a note
    // s037: looking at a project is not changing it. A write that touches only
    // the review fields leaves `modified` alone, as the Inbox mark does.
    Task same = n->task;
    same.review   = t.review;
    same.reviewed = t.reviewed;
    const bool only_review = (same == t) && !rolled;
    n->task     = t;
    if (!only_review) n->modified = now();
    notify(Change::Task, id);
    if (rolled) {
        // A repeating project starts over: its ticked steps untick. Through
        // the virtual, so a persistent store writes each one.
        std::vector<NodeId> under;
        collect_subtree(id, under);
        for (const auto& d : under) {
            if (d == id) continue;
            const Node* k = find(d);
            if (!k || !k->task.is_task || !k->task.done) continue;
            Task kt = k->task;
            kt.done = false;
            set_task(d, kt);
        }
    }
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
bool NodeSource::set_repeat(const NodeId& id, const Repeat& r) {
    return edit_task(*this, id, [&](Task& t) { t.repeat = r; });
}
bool NodeSource::set_estimate(const NodeId& id, int minutes) {
    if (minutes < 0) return false;
    return edit_task(*this, id, [&](Task& t) { t.estimate = minutes; });
}
const std::vector<LogRecord>& NodeSource::history() const {
    static const std::vector<LogRecord> kNone;
    return kNone;
}
bool NodeSource::set_status(const NodeId& id, Status s) {
    return edit_task(*this, id, [&](Task& t) { t.status = s; });
}
bool NodeSource::make_task(const NodeId& id, bool on) {
    return edit_task(*this, id, [&](Task& t) {
        if (on) {
            t.is_task = true;
            // s031: a completed note-project becomes a DONE todo -- a todo is
            // finished by `done`, never by the project word (one way, not two).
            if (t.project == ProjectState::Completed) {
                t.done = true;
                t.project = ProjectState::Active;
            }
            return;
        }
        // Clear everything EXCEPT status and project state: both describe the
        // node as a CONTAINER, and a note that stops being a todo is still
        // their container. (`done` goes with the rest: un-making a ticked
        // todo gives a plain note, not a "completed" one.)
        const Status       keep    = t.status;
        const ProjectState project = t.project;
        const Repeat       review  = t.review;     // s037: the container's, too
        const std::int64_t looked  = t.reviewed;
        const ProjectMark  mark    = t.mark;       // s037b
        const Task         was     = t;
        t = Task{};
        t.status   = keep;
        t.project  = project;
        t.review   = review;
        t.reviewed = looked;
        t.mark     = mark;
        // s037b: a project said on purpose keeps its dates and flag -- they
        // are the project's, and it is still a project.
        if (mark == ProjectMark::On) {
            t.due     = was.due;
            t.defer   = was.defer;
            t.flagged = was.flagged;
        }
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
    // s045: NOT reindex(). Sibling order lives in m_kids, and a move changes
    // m_kids without touching storage order -- so rebuilding m_kids from
    // storage here quietly undid every earlier reorder among the survivors.
    // Take the doomed ids out of the lists that hold them, and rebuild only
    // the id -> index map (storage indices shifted).
    for (auto& [p, kids] : m_kids)
        std::erase_if(kids, [&](const NodeId& k) {
            return std::find(doomed.begin(), doomed.end(), k) != doomed.end();
        });
    for (const auto& d : doomed) m_kids.erase(d);
    m_by_id.clear();
    for (std::size_t i = 0; i < m_nodes.size(); ++i) m_by_id[m_nodes[i].id] = i;
    notify(Change::Removed, id);
    return true;
}

bool MemoryNodes::restore(const Node& n, int index) {
    if (n.id.empty() || m_by_id.count(n.id)) return false;
    if (!n.parent_id.empty() && !m_by_id.count(n.parent_id)) return false;
    m_by_id[n.id] = m_nodes.size();
    m_nodes.push_back(n);
    auto& sib = m_kids[n.parent_id];
    const int at = (index < 0 || index > static_cast<int>(sib.size()))
                       ? static_cast<int>(sib.size()) : index;
    sib.insert(sib.begin() + at, n.id);
    notify(Change::Created, n.id);
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
    Gesture step(src, "Capture");   // s046b: create + body + mark, one Ctrl+Z
    std::string title, body;
    capture_split(text, title, body);
    if (title.empty()) return {};          // nothing typed is not a note
    const NodeId id = src.create("", title);
    if (id.empty()) return id;
    if (!body.empty()) src.set_body(id, body);
    src.set_inbox(id, true);
    return id;
}

namespace {
std::string trimmed(std::string s) {
    for (auto& c : s) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    const auto a = s.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(' ') - a + 1);
}
std::string folded(const std::string& s) {
    std::string o = trimmed(s);
    for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
}  // namespace

std::string append_tasks(const std::string& body, const std::vector<std::string>& items) {
    std::string out = body;
    for (const auto& raw : items) {
        const std::string it = trimmed(raw);
        if (it.empty()) continue;
        if (!out.empty() && out.back() != '\n') out += '\n';
        out += "- [ ] " + it + "\n";
    }
    return out;
}

std::string append_text(const std::string& body, const std::string& text) {
    const std::string t = trimmed(text);
    if (t.empty()) return body;
    std::string out = body;
    if (!out.empty() && out.back() != '\n') out += '\n';
    // The last line: a list item (- * + or 1. 1)) would swallow the new line.
    if (!out.empty()) {
        const auto start = out.size() >= 2 ? out.find_last_of('\n', out.size() - 2) : std::string::npos;
        std::string last = out.substr(start == std::string::npos ? 0 : start + 1);
        const auto a = last.find_first_not_of(" \t");
        last = a == std::string::npos ? std::string{} : last.substr(a);
        bool item = last.size() >= 2 && (last[0] == '-' || last[0] == '*' || last[0] == '+') && last[1] == ' ';
        std::size_t d = 0;
        while (d < last.size() && std::isdigit(static_cast<unsigned char>(last[d]))) ++d;
        if (d > 0 && d + 1 < last.size() && (last[d] == '.' || last[d] == ')') && last[d + 1] == ' ') item = true;
        if (item) out += '\n';
    }
    return out + t + "\n";
}

NodeId capture_append(NodeSource& src, const std::string& name, const std::string& text,
                      bool* appended) {
    Gesture step(src, "Append");    // s046b
    if (appended) *appended = false;
    const std::string title = trimmed(name);
    if (title.empty() || trimmed(text).empty()) return {};
    NodeId id = find_list_note(src, title);
    if (!id.empty()) {
        const Node* n = src.find(id);
        if (!n || !src.set_body(id, append_text(n->body, text))) return {};
        if (appended) *appended = true;
        return id;
    }
    id = src.create("", title);
    if (!id.empty()) { src.set_body(id, append_text("", text)); src.set_inbox(id, true); }
    return id;
}

NodeId find_list_note(const NodeSource& src, const std::string& title) {
    const std::string want = folded(title);
    if (want.empty()) return {};
    // Tree order, depth first -- the order the user sees the rows in, so
    // "the first one" is the one nearest the top of the tree.
    std::vector<NodeId> stack;
    auto push_kids = [&](const NodeId& parent) {
        auto kids = src.children(parent);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    };
    push_kids("");
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (const Node* n = src.find(id))
            if (!n->protect && folded(n->title) == want) return id;
        push_kids(id);
    }
    return {};
}

NodeId capture_list(NodeSource& src, const std::string& name,
                    const std::vector<std::string>& items, bool* appended) {
    Gesture step(src, "List");      // s046b
    if (appended) *appended = false;
    const std::string title = trimmed(name);
    const std::string add = append_tasks("", items);
    if (title.empty() || add.empty()) return {};
    NodeId id = find_list_note(src, title);
    if (!id.empty()) {
        const Node* n = src.find(id);
        if (!n || !src.set_body(id, append_tasks(n->body, items))) return {};
        if (appended) *appended = true;
        return id;
    }
    id = src.create("", title);
    if (!id.empty()) { src.set_body(id, add); src.set_inbox(id, true); }
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
