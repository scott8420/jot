#include "core/Selection.hpp"
#include "core/Markdown.hpp"
#include "core/Tags.hpp"

#include <algorithm>
#include <functional>
#include <set>

namespace jot::core {

std::vector<NodeId> in_document_order(const NodeSource& src, const std::vector<NodeId>& ids) {
    std::set<NodeId> want;
    for (const auto& id : ids)
        if (src.find(id)) want.insert(id);
    std::vector<NodeId> out;
    if (want.empty()) return out;
    std::function<void(const NodeId&)> walk = [&](const NodeId& p) {
        for (const auto& k : src.children(p)) {
            if (out.size() == want.size()) return;
            if (want.count(k)) out.push_back(k);
            walk(k);
        }
    };
    walk("");
    return out;
}

std::vector<NodeId> selection_roots(const NodeSource& src, const std::vector<NodeId>& ids) {
    const std::vector<NodeId> ordered = in_document_order(src, ids);
    const std::set<NodeId> chosen(ordered.begin(), ordered.end());
    std::vector<NodeId> out;
    for (const auto& id : ordered) {
        bool under = false;
        for (const Node* p = src.find(src.find(id)->parent_id); p && !under; p = src.find(p->parent_id))
            under = chosen.count(p->id) != 0;
        if (!under) out.push_back(id);
    }
    return out;
}

NodeId neighbour_after_delete_all(const NodeSource& src, const std::vector<NodeId>& ids) {
    const std::vector<NodeId> roots = selection_roots(src, ids);
    if (roots.empty()) return {};
    const std::set<NodeId> going(roots.begin(), roots.end());
    // After the last root, among its siblings.
    {
        const Node* last = src.find(roots.back());
        const auto kids = src.children(last->parent_id);
        auto it = std::find(kids.begin(), kids.end(), roots.back());
        if (it != kids.end())
            for (++it; it != kids.end(); ++it)
                if (!going.count(*it)) return *it;
    }
    // Before the first root.
    const Node* first = src.find(roots.front());
    const auto kids = src.children(first->parent_id);
    auto it = std::find(kids.begin(), kids.end(), roots.front());
    while (it != kids.begin()) {
        --it;
        if (!going.count(*it)) return *it;
    }
    return first->parent_id;   // a root's parent is never going (else it would not be a root)
}

namespace {
template <class T>
void meet(Shared<T>& s, const T& v, bool first) {
    if (first) { s.value = v; return; }
    if (!(s.value == v)) s.mixed = true;
}
}  // namespace

Common common(const NodeSource& src, const std::vector<NodeId>& ids) {
    Common c;
    std::vector<std::string> keys;   // first-seen order
    std::vector<std::string> names;
    std::vector<std::size_t> carried;
    std::set<NodeId> seen;
    for (const auto& id : ids) {
        const Node* n = src.find(id);
        if (!n || !seen.insert(id).second) continue;
        meet(c.is_task, n->task.is_task, c.count == 0);
        ++c.count;
        if (n->protect) ++c.locked;
        if (n->task.is_task) {
            const bool first = c.todos == 0;
            meet(c.done, n->task.done, first);
            meet(c.flagged, n->task.flagged, first);
            meet(c.due, n->task.due, first);
            meet(c.defer, n->task.defer, first);
            meet(c.estimate, n->task.estimate, first);
            meet(c.repeat, n->task.repeat, first);
            ++c.todos;
        }
        std::set<std::string> mine;
        for (const auto& t : node_tags(*n)) {   // s062: the list, then the text
            const std::string k = tag_key(t);
            if (k.empty() || !mine.insert(k).second) continue;
            auto it = std::find(keys.begin(), keys.end(), k);
            if (it == keys.end()) {
                keys.push_back(k);
                names.push_back(t);
                carried.push_back(1);
            } else {
                ++carried[static_cast<std::size_t>(it - keys.begin())];
            }
        }
    }
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (carried[i] == c.count) c.tags_all.push_back(names[i]);
        else                       c.tags_some.emplace_back(names[i], carried[i]);
    }
    return c;
}

std::vector<NodeId> writable(const NodeSource& src, const std::vector<NodeId>& ids, bool todos_only) {
    std::vector<NodeId> out;
    std::set<NodeId> seen;
    for (const auto& id : ids) {
        const Node* n = src.find(id);
        if (!n || n->protect || (todos_only && !n->task.is_task)) continue;
        if (seen.insert(id).second) out.push_back(id);
    }
    return out;
}

std::vector<std::pair<NodeId, FmtEdit>> tag_edits(const NodeSource& src, const std::vector<NodeId>& ids,
                                                  const std::string& name, bool add) {
    std::vector<std::pair<NodeId, FmtEdit>> out;
    for (const auto& id : writable(src, ids, false)) {
        const Node* n = src.find(id);
        FmtEdit e = add ? tag_add_edit(n->body, name) : tag_remove_edit(n->body, name);
        if (e.ok) out.emplace_back(id, std::move(e));
    }
    return out;
}

std::string many_label(const std::string& what, std::size_t n) {
    return n > 1 ? what + " " + std::to_string(n) + " notes" : what;
}

}  // namespace jot::core
