#include "core/Selection.hpp"

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

}  // namespace jot::core
