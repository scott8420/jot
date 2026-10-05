#include "core/Inbox.hpp"
#include "core/Tasks.hpp"
#include "core/Undo.hpp"

namespace jot::core {

InboxState inbox_state(const Node& n) {
    // Done beats Filed: a ticked todo under a project is "done" first -- that
    // is the sentence you want to read on its row.
    if (n.task.is_task && n.task.done) return InboxState::Done;
    if (!n.parent_id.empty())          return InboxState::Filed;
    return InboxState::Waiting;
}

std::vector<NodeId> inbox_members(const NodeSource& src) {
    std::vector<NodeId> out;
    std::vector<NodeId> stack;
    auto push_kids = [&](const NodeId& parent) {
        auto kids = src.children(parent);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    };
    push_kids("");
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (const Node* n = src.find(id); n && n->inbox) out.push_back(id);
        push_kids(id);
    }
    return out;
}

InboxCounts inbox_counts(const NodeSource& src) {
    InboxCounts c;
    for (const auto& id : inbox_members(src))
        if (const Node* n = src.find(id))
            (inbox_processed(*n) ? c.ready : c.waiting)++;
    return c;
}

std::size_t clean_up(NodeSource& src) {
    Gesture step(src, "Clean Up");  // s046b: every mark it clears, one Ctrl+Z
    // Collect first, write second: each set_inbox notifies, and a listener
    // that re-queries mid-walk must not see a half-swept list.
    std::vector<NodeId> take;
    for (const auto& id : inbox_members(src))
        if (const Node* n = src.find(id); n && inbox_processed(*n)) take.push_back(id);
    std::size_t cleared = 0;
    for (const auto& id : take)
        if (src.set_inbox(id, false)) ++cleared;
    return cleared;
}

std::string age_phrase(std::int64_t then, std::int64_t now) {
    if (then <= 0) return {};
    const std::int64_t d = now - then;
    if (d < 60)          return "just now";            // also a clock that went backwards
    if (d < 3600)        return std::to_string(d / 60) + " min ago";
    if (d < 86400)       return std::to_string(d / 3600) + " h ago";
    if (d < 2 * 86400)   return "1 day ago";       // not "yesterday": that is a calendar claim
    if (d < 14 * 86400)  return std::to_string(d / 86400) + " days ago";
    return format_date(then);
}

}  // namespace jot::core
