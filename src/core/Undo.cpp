#include "core/Undo.hpp"

#include <algorithm>
#include <chrono>
#include <regex>
#include <set>
#include <utility>
#include <sstream>

namespace jot::core {
namespace {

int index_of(const NodeSource& src, const NodeId& id, const NodeId& parent) {
    const auto kids = src.children(parent);
    const auto it = std::find(kids.begin(), kids.end(), id);
    return it == kids.end() ? -1 : static_cast<int>(it - kids.begin());
}

int depth_of(const NodeSource& src, const Node& n) {
    int d = 0;
    for (const Node* p = src.find(n.parent_id); p && d < 10000; p = src.find(p->parent_id)) ++d;
    return d;
}

void collect(const NodeSource& src, const NodeId& id, std::vector<NodeId>& out) {
    out.push_back(id);
    for (const auto& k : src.children(id)) collect(src, k, out);
}

NodeSnap snap_one(const NodeSource& src, const NodeId& id) {
    NodeSnap s;
    s.id = id;
    if (const Node* n = src.find(id)) {
        s.present = true;
        s.node    = *n;
        s.index   = index_of(src, id, n->parent_id);
        s.depth   = depth_of(src, *n);
    }
    return s;
}

}  // namespace

Snapshot snapshot(const NodeSource& src, const std::vector<NodeId>& ids, bool subtrees) {
    std::vector<NodeId> all;
    for (const auto& id : ids) {
        if (subtrees && src.find(id)) collect(src, id, all);
        else all.push_back(id);
    }
    Snapshot out;
    std::set<NodeId> seen;
    for (const auto& id : all)
        if (seen.insert(id).second) out.push_back(snap_one(src, id));
    return out;
}

bool apply(NodeSource& src, const Snapshot& want, const Snapshot* other) {
    bool ok = true;
    const auto counterpart = [&](const NodeId& id) -> const NodeSnap* {
        if (!other) return nullptr;
        for (const auto& o : *other) if (o.id == id) return &o;
        return nullptr;
    };

    // 1. Absent: remove what should not be here. Deepest first, and only what
    //    is still present (removing a parent took its children with it).
    std::vector<const NodeSnap*> gone;
    for (const auto& s : want) if (!s.present) gone.push_back(&s);
    std::sort(gone.begin(), gone.end(), [&](const NodeSnap* a, const NodeSnap* b) {
        const Node* na = src.find(a->id);
        const Node* nb = src.find(b->id);
        return (na ? depth_of(src, *na) : 0) > (nb ? depth_of(src, *nb) : 0);
    });
    for (const NodeSnap* s : gone)
        if (src.find(s->id) && !src.remove(s->id)) ok = false;

    // 2. Present: parents before children, and within one parent in index
    //    order, so each lands at its index among siblings already in place.
    std::vector<const NodeSnap*> here;
    for (const auto& s : want) if (s.present) here.push_back(&s);
    std::sort(here.begin(), here.end(), [](const NodeSnap* a, const NodeSnap* b) {
        if (a->depth != b->depth) return a->depth < b->depth;
        return a->index < b->index;
    });
    for (const NodeSnap* s : here) {
        const Node& w = s->node;
        if (!src.find(s->id)) {
            if (!src.restore(w, s->index)) ok = false;
            continue;
        }
        // Only what this step changed, when the other side is known and the
        // node was present on both sides.
        const NodeSnap* o = counterpart(s->id);
        const bool whole = !o || !o->present;
        const Node* ov = whole ? nullptr : &o->node;
        const auto changed = [&](auto field) { return whole || !(field(w) == field(*ov)); };

        const bool was_locked = src.find(s->id)->protect;
        if (was_locked) src.set_protect(s->id, false);   // unlock to write; relock below
        const Node cur = *src.find(s->id);
        if (changed([](const Node& n) { return n.title; })  && cur.title != w.title)   src.set_title(s->id, w.title);
        if (changed([](const Node& n) { return n.body; })   && cur.body != w.body)     src.set_body(s->id, w.body);
        if (changed([](const Node& n) { return n.task; })   && !(cur.task == w.task))  src.set_task(s->id, w.task);
        if (changed([](const Node& n) { return n.inbox; })  && cur.inbox != w.inbox)   src.set_inbox(s->id, w.inbox);
        if (changed([](const Node& n) { return n.packet; }) && cur.packet != w.packet) src.set_packet(s->id, w.packet);
        if (changed([](const Node& n) { return std::make_pair(n.sent, n.sent_to); }) &&
            (cur.sent != w.sent || cur.sent_to != w.sent_to))
            src.set_sent(s->id, w.sent, w.sent_to);
        if (changed([](const Node& n) { return n.nudge; }) && cur.nudge != w.nudge) src.set_nudge(s->id, w.nudge);
        // Place. move() counts the index BEFORE taking the node out, so a
        // later place among the same siblings needs one more.
        const bool place_changed = whole || o->node.parent_id != w.parent_id || o->index != s->index;
        const int now_at = index_of(src, s->id, cur.parent_id);
        if (place_changed && (cur.parent_id != w.parent_id || now_at != s->index)) {
            int to = s->index;
            if (cur.parent_id == w.parent_id && to > now_at) ++to;
            if (!src.move(s->id, w.parent_id, to)) ok = false;
        }
        const bool want_lock = changed([](const Node& n) { return n.protect; }) ? w.protect : was_locked;
        if (want_lock) src.set_protect(s->id, true);
    }
    return ok;
}

NodeId neighbour_after_delete(const NodeSource& src, const NodeId& id) {
    const Node* n = src.find(id);
    if (!n) return {};
    const auto kids = src.children(n->parent_id);
    const auto it = std::find(kids.begin(), kids.end(), id);
    if (it == kids.end()) return n->parent_id;
    if (it + 1 != kids.end()) return *(it + 1);
    if (it != kids.begin()) return *(it - 1);
    return n->parent_id;
}

double Journal::now() const {
    if (m_clock) return m_clock();
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// s046b: run is a gesture with its ids touched up front -- the same pictures
// as s045's, and any write the op makes through an UndoSource folds in.
bool Journal::run(NodeSource& src, const std::string& label, const std::vector<NodeId>& ids,
                  bool subtrees, const Op& op) {
    if (m_quiet) { op(); return true; }
    begin(src, label);
    touch(src, ids, subtrees);
    for (const auto& c : op()) made(c);
    return commit(src);
}

void Journal::begin(const NodeSource& src, const std::string& label, const std::string& merge) {
    if (m_quiet) return;
    if (m_depth++ > 0) return;          // nested: the outer gesture names the step
    guard(src);
    m_open = UndoStep{};
    m_open.label = label;
    m_open.merge = merge;
    if (m_selection_of) m_open.selection = m_selection_of();   // s049
    m_open_seen.clear();
}

void Journal::touch(const NodeSource& src, const std::vector<NodeId>& ids, bool subtrees) {
    if (m_quiet || m_depth == 0) return;
    std::vector<NodeId> all;
    for (const auto& id : ids) {
        if (id.empty()) continue;
        if (subtrees && src.find(id)) collect(src, id, all);
        else all.push_back(id);
    }
    for (const auto& id : all) {
        if (std::find(m_open_seen.begin(), m_open_seen.end(), id) != m_open_seen.end()) continue;
        m_open_seen.push_back(id);
        m_open.before.push_back(snap_one(src, id));
    }
}

void Journal::made(const NodeId& id) {
    if (m_quiet || m_depth == 0 || id.empty()) return;
    if (std::find(m_open_seen.begin(), m_open_seen.end(), id) != m_open_seen.end()) return;
    m_open_seen.push_back(id);
    NodeSnap none;
    none.id = id;
    m_open.before.push_back(none);
}

bool Journal::commit(const NodeSource& src) {
    if (m_quiet || m_depth == 0) return false;
    if (--m_depth > 0) return false;    // the outer gesture is still open
    UndoStep st = std::move(m_open);
    m_open = UndoStep{};
    m_open_seen.clear();

    // AFTER covers the same nodes, in the same order.
    std::vector<NodeId> after_ids;
    for (const auto& s : st.before) after_ids.push_back(s.id);
    st.after = snapshot(src, after_ids, false);
    st.focus = !st.before.empty() ? st.before.front().id : NodeId{};
    st.when  = now();

    // Nothing changed (a refused delete, a move onto itself): not a step.
    bool same = st.before.size() == st.after.size();
    for (std::size_t i = 0; same && i < st.before.size(); ++i) {
        const auto& a = st.before[i];
        const auto& b = st.after[i];
        same = a.present == b.present && a.index == b.index &&
               (!a.present || (a.node.parent_id == b.node.parent_id && a.node.title == b.node.title &&
                               a.node.body == b.node.body && a.node.task == b.node.task &&
                               a.node.inbox == b.node.inbox && a.node.packet == b.node.packet &&
                               a.node.sent == b.node.sent && a.node.sent_to == b.node.sent_to &&
                               a.node.nudge == b.node.nudge &&
                               a.node.protect == b.node.protect));
    }
    if (same) return false;

    // Fold into the newest step: same key, soon, and nothing undone since.
    if (!st.merge.empty() && m_at == m_steps.size() && !m_steps.empty()) {
        UndoStep& last = m_steps.back();
        bool same_nodes = last.after.size() == st.after.size();
        for (std::size_t i = 0; same_nodes && i < st.after.size(); ++i)
            same_nodes = last.after[i].id == st.after[i].id;
        if (last.merge == st.merge && same_nodes && st.when - last.when <= kMergeSeconds) {
            last.after = std::move(st.after);
            last.when  = st.when;
            changed();
            return true;
        }
    }

    m_steps.resize(m_at);               // a new step forgets what was undone
    m_steps.push_back(std::move(st));
    if (m_steps.size() > kMax) m_steps.erase(m_steps.begin());
    m_at = m_steps.size();
    changed();
    return true;
}

std::string Journal::undo_label() const { return can_undo() ? m_steps[m_at - 1].label : ""; }
std::string Journal::redo_label() const { return can_redo() ? m_steps[m_at].label : ""; }

std::string Journal::undo(NodeSource& src) {
    guard(src);
    if (!can_undo()) return {};
    m_quiet = true;
    const UndoStep& st = m_steps[--m_at];
    apply(src, st.before, &st.after);
    m_last_focus = st.focus;
    m_last_selection = st.selection;   // s049
    m_quiet = false;
    changed();
    return st.label;
}

std::string Journal::redo(NodeSource& src) {
    guard(src);
    if (!can_redo()) return {};
    m_quiet = true;
    const UndoStep& st = m_steps[m_at++];
    apply(src, st.after, &st.before);
    m_last_focus = st.focus;
    m_last_selection = st.selection;   // s049
    m_quiet = false;
    changed();
    return st.label;
}

void Journal::clear() {
    m_last_selection.clear();   // s049
    m_steps.clear();
    m_at = 0;
    m_depth = 0;
    m_open = UndoStep{};
    m_open_seen.clear();
    changed();
}

// ── task_label ──────────────────────────────────────────────────────────────
std::string task_label(const Task& a, const Task& b) {
    if (a.is_task != b.is_task)   return b.is_task ? "Make a todo" : "Not a todo";
    if (a.done != b.done)         return b.done ? "Tick" : "Untick";
    if (a.flagged != b.flagged)   return b.flagged ? "Flag" : "Unflag";
    if (a.due != b.due)           return "Due date";
    if (a.defer != b.defer)       return "Defer date";
    if (a.estimate != b.estimate) return "Estimate";
    if (!(a.repeat == b.repeat))  return "Repeat";
    if (a.status != b.status)     return "Children";
    if (a.project != b.project)   return "Project status";
    if (a.mark != b.mark)         return b.mark == ProjectMark::On ? "Is a project"
                                       : b.mark == ProjectMark::Off ? "Not a project" : "Project";
    if (!(a.review == b.review))  return "Review interval";
    if (a.reviewed != b.reviewed) return "Mark reviewed";
    return "Edit todo";
}

// ── UndoSource ──────────────────────────────────────────────────────────────
namespace {
const std::vector<LogRecord>& no_history() {
    static const std::vector<LogRecord> none;
    return none;
}

std::string quoted(const NodeSource& src, const NodeId& id) {
    const Node* n = src.find(id);
    const std::string t = n && !n->title.empty() ? n->title : std::string("Untitled");
    return "\u201c" + t + "\u201d";
}

// One write, as one step unless a gesture is already open (then it is part of
// that one). `before` touches what the write is about to change.
template <class Fn>
auto recorded(Journal& j, NodeSource& in, const std::string& label, const std::string& merge,
              const std::vector<NodeId>& ids, bool subtrees, Fn&& fn) {
    j.begin(in, label, merge);
    j.touch(in, ids, subtrees);
    auto r = fn();
    j.commit(in);
    return r;
}
}  // namespace

std::vector<NodeId> UndoSource::children(const NodeId& p) const {
    const NodeSource* in = inner();
    return in ? in->children(p) : std::vector<NodeId>{};
}
const Node* UndoSource::find(const NodeId& id) const {
    const NodeSource* in = inner();
    return in ? in->find(id) : nullptr;
}
std::size_t UndoSource::count() const {
    const NodeSource* in = inner();
    return in ? in->count() : 0;
}
const std::vector<LogRecord>& UndoSource::history() const {
    const NodeSource* in = inner();
    return in ? in->history() : no_history();
}

NodeId UndoSource::create(const NodeId& parent, const std::string& title) {
    NodeSource* in = inner();
    if (!in) return {};
    m_j.begin(*in, "New note");
    const NodeId id = in->create(parent, title);
    m_j.made(id);
    m_j.commit(*in);
    return id;
}
bool UndoSource::set_title(const NodeId& id, const std::string& title) {
    NodeSource* in = inner();
    if (!in) return false;
    // Keystrokes in a name field fold into one step per field.
    return recorded(m_j, *in, "Rename", "title:" + id, {id}, false,
                    [&] { return in->set_title(id, title); });
}
bool UndoSource::set_body(const NodeId& id, const std::string& body) {
    NodeSource* in = inner();
    if (!in) return false;
    return recorded(m_j, *in, "Edit text", "", {id}, false, [&] { return in->set_body(id, body); });
}
bool UndoSource::set_protect(const NodeId& id, bool on) {
    NodeSource* in = inner();
    if (!in) return false;
    return recorded(m_j, *in, on ? "Protect" : "Unprotect", "", {id}, false,
                    [&] { return in->set_protect(id, on); });
}
bool UndoSource::set_task(const NodeId& id, const Task& t) {
    NodeSource* in = inner();
    if (!in) return false;
    const Node* n = in->find(id);
    const std::string label = n ? task_label(n->task, t) : std::string("Edit todo");
    return recorded(m_j, *in, label, "", {id}, false, [&] { return in->set_task(id, t); });
}
bool UndoSource::set_inbox(const NodeId& id, bool on) {
    NodeSource* in = inner();
    if (!in) return false;
    return recorded(m_j, *in, on ? "Into the Inbox" : "Out of the Inbox", "", {id}, false,
                    [&] { return in->set_inbox(id, on); });
}
bool UndoSource::set_packet(const NodeId& id, bool on) {
    NodeSource* in = inner();
    if (!in) return false;
    return recorded(m_j, *in, on ? "Packet" : "Not a packet", "", {id}, false,
                    [&] { return in->set_packet(id, on); });
}
bool UndoSource::set_sent(const NodeId& id, std::int64_t when, const std::string& to) {
    NodeSource* in = inner();
    if (!in) return false;
    return recorded(m_j, *in, when ? "Sent" : "Not sent", "", {id}, false,
                    [&] { return in->set_sent(id, when, to); });
}
bool UndoSource::set_nudge(const NodeId& id, int days) {
    NodeSource* in = inner();
    if (!in) return false;
    return recorded(m_j, *in, days ? "Nudge" : "No nudge", "", {id}, false,
                    [&] { return in->set_nudge(id, days); });
}
bool UndoSource::move(const NodeId& id, const NodeId& new_parent, int index) {
    NodeSource* in = inner();
    if (!in) return false;
    return recorded(m_j, *in, "Move", "", {id}, true,
                    [&] { return in->move(id, new_parent, index); });
}
bool UndoSource::remove(const NodeId& id) {
    NodeSource* in = inner();
    if (!in) return false;
    return recorded(m_j, *in, "Delete " + quoted(*in, id), "", {id}, true,
                    [&] { return in->remove(id); });
}
bool UndoSource::restore(const Node& n, int index) {
    NodeSource* in = inner();
    if (!in) return false;
    m_j.begin(*in, "Restore");
    const bool ok = in->restore(n, index);
    if (ok) m_j.made(n.id);
    m_j.commit(*in);
    return ok;
}

bool as_step(NodeSource& src, const std::string& label, const std::vector<NodeId>& ids,
             bool subtrees, const std::function<void()>& fn, const std::string& merge) {
    auto* u = dynamic_cast<UndoSource*>(&src);
    NodeSource* in = u ? u->inner() : nullptr;
    if (!in) { fn(); return false; }
    Journal& j = u->journal();
    j.begin(*in, label, merge);
    j.touch(*in, ids, subtrees);
    fn();
    return j.commit(*in);
}

Gesture::Gesture(NodeSource& src, const std::string& label, const std::string& merge) {
    m_u = dynamic_cast<UndoSource*>(&src);
    if (m_u && m_u->inner()) m_u->journal().begin(*m_u->inner(), label, merge);
    else m_u = nullptr;
}
Gesture::~Gesture() {
    if (m_u && m_u->inner()) m_u->journal().commit(*m_u->inner());
}
void Gesture::touch(const std::vector<NodeId>& ids, bool subtrees) {
    if (m_u && m_u->inner()) m_u->journal().touch(*m_u->inner(), ids, subtrees);
}

// ── raw_writes ──────────────────────────────────────────────────────────────
std::vector<std::string> raw_writes(const std::string& file, const std::string& text) {
    static const std::regex write_on_store(
        R"(m_store\s*->\s*(set_title|set_body|set_protect|set_task|set_inbox|set_packet|set_sent|set_nudge|set_done|)"
        R"(set_flagged|set_due|set_defer|set_status|set_repeat|set_estimate|make_task|move|remove|)"
        R"(restore|create)\s*\()");
    static const std::regex store_to_writer(
        R"(core::(capture|capture_list|capture_append|clean_up|set_project_state|set_project_mark|)"
        R"(mark_reviewed|set_review_every|retarget\w*)\s*\(\s*\*\s*m_store)");
    static const std::regex store_to_pane(R"(set_source\s*\(\s*m_store)");
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line, prev;
    int no = 0;
    while (std::getline(in, line)) {
        ++no;
        const bool marked = line.find("// raw:") != std::string::npos ||
                            prev.find("// raw:") != std::string::npos;
        if (!marked && (std::regex_search(line, write_on_store) ||
                        std::regex_search(line, store_to_writer) ||
                        std::regex_search(line, store_to_pane))) {
            const auto b = line.find_first_not_of(" \t");
            out.push_back(file + ":" + std::to_string(no) + ": " +
                          (b == std::string::npos ? line : line.substr(b)));
        }
        prev = line;
    }
    return out;
}

}  // namespace jot::core
