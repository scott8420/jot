#include "core/Undo.hpp"

#include <algorithm>
#include <set>

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

bool Journal::run(NodeSource& src, const std::string& label, const std::vector<NodeId>& ids,
                  bool subtrees, const Op& op) {
    if (m_quiet) { op(); return true; }
    guard(src);
    UndoStep st;
    st.label  = label;
    st.before = snapshot(src, ids, subtrees);
    const auto created = op();

    // AFTER covers the same nodes (and whatever was made); BEFORE gains an
    // "absent" entry for each node that did not exist yet.
    std::vector<NodeId> after_ids;
    for (const auto& s : st.before) after_ids.push_back(s.id);
    for (const auto& c : created)
        if (!c.empty()) {
            after_ids.push_back(c);
            NodeSnap none;
            none.id = c;
            st.before.push_back(none);
        }
    st.after = snapshot(src, after_ids, false);
    st.focus = !ids.empty() ? ids.front() : (!created.empty() ? created.front() : NodeId{});
    // A delete leaves nodes absent; their AFTER entries say so already.

    // Nothing changed (a refused delete, a move onto itself): not a step.
    bool same = st.before.size() == st.after.size();
    for (std::size_t i = 0; same && i < st.before.size(); ++i) {
        const auto& a = st.before[i];
        const auto& b = st.after[i];
        same = a.present == b.present && a.index == b.index &&
               (!a.present || (a.node.parent_id == b.node.parent_id && a.node.title == b.node.title &&
                               a.node.body == b.node.body && a.node.task == b.node.task &&
                               a.node.inbox == b.node.inbox && a.node.packet == b.node.packet &&
                               a.node.protect == b.node.protect));
    }
    if (same) return false;

    m_steps.resize(m_at);               // a new step forgets what was undone
    m_steps.push_back(std::move(st));
    if (m_steps.size() > kMax) m_steps.erase(m_steps.begin());
    m_at = m_steps.size();
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
    m_quiet = false;
    return st.label;
}

std::string Journal::redo(NodeSource& src) {
    guard(src);
    if (!can_redo()) return {};
    m_quiet = true;
    const UndoStep& st = m_steps[m_at++];
    apply(src, st.after, &st.before);
    m_last_focus = st.focus;
    m_quiet = false;
    return st.label;
}

void Journal::clear() {
    m_steps.clear();
    m_at = 0;
}

}  // namespace jot::core
