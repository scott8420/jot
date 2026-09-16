#include "core/Notify.hpp"
#include "core/Markdown.hpp"

#include <algorithm>
#include <string>

namespace jot::core {
namespace {

std::string titled(const Node* n) {
    if (!n) return {};
    return n->title.empty() ? std::string("Untitled") : n->title;
}

// The body of the notification. NOT the projection's detail line, and the
// difference is the availability word: this half already filtered on it, so
// repeating it would be telling the reader something the mere existence of the
// notification has said. What is left is the WHY -- the parent's title and the
// first line of its note, which is the sentence a task manager's notification
// normally cannot carry because its note field is a scratch pad and jot's
// parent IS a note.
std::string detail_for(const NodeSource& src, const Node& n) {
    const Node* parent = n.parent_id.empty() ? nullptr : src.find(n.parent_id);
    if (!parent) return {};

    std::string out = titled(parent);
    const std::string why = first_prose_line(parent->body);
    if (!why.empty()) out += "\n" + why;
    return out;
}

}  // namespace

std::string announce_key(const NodeId& id, std::int64_t due) {
    return id + "@" + std::to_string(due);
}

AnnounceResult due_announcements(const NodeSource& src, const TaskIndex& tasks,
                                 std::int64_t now,
                                 const std::vector<std::string>& announced) {
    AnnounceResult out;

    for (const NodeId& id : tasks.tasks()) {
        const Node* n = src.find(id);
        if (!n || !n->task.is_task) continue;   // the index holds membership; trust but check
        if (n->task.done) continue;

        const std::int64_t due = effective_due(src, id);
        if (due == 0)  continue;                // no deadline, nothing to arrive
        if (due > now) continue;                // not yet

        // THE ASYMMETRY WITH THE PROJECTION. See Notify.hpp.
        if (availability(src, id, now) != Avail::Available) continue;

        const std::string key = announce_key(id, due);
        out.keep.push_back(key);

        if (std::find(announced.begin(), announced.end(), key) != announced.end())
            continue;                           // already said once; say nothing

        Announcement a;
        a.id      = id;
        a.key     = key;
        a.summary = titled(n);
        a.detail  = detail_for(src, *n);
        a.due     = due;
        // OVERDUE means a day has passed, not a second. A task due at 17:00
        // becomes due AT 17:00 and is not "overdue" at 17:01 -- calling it that
        // would make every notification jot ever sends an overdue one.
        a.overdue = due < day_start(now);
        a.flagged = n->task.flagged;
        out.to_show.push_back(std::move(a));
    }

    return out;
}

}  // namespace jot::core
