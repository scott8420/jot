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
        out.live.push_back(key);

        if (std::find(announced.begin(), announced.end(), key) != announced.end()) {
            // Said once AND acknowledged -- it stays in the announced set for as
            // long as it is live, and nothing more is sent. A key that was only
            // SENT is not in here, which is the whole of s015: it falls through
            // and is offered again next tick.
            out.keep.push_back(key);
            continue;
        }

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

// ─────────────────────────────────────────────────────────────────────────────
// Outbox. A vector rather than a map: this holds the deadlines that are due
// RIGHT NOW and not yet acknowledged, which is nearly always zero and has never
// plausibly been more than a handful. A linear scan over that is free, and the
// order it preserves makes the selftest's assertions readable.
// ─────────────────────────────────────────────────────────────────────────────

Outbox::Entry* Outbox::find(const std::string& key) {
    for (auto& e : m_entries) if (e.key == key) return &e;
    return nullptr;
}

const Outbox::Entry* Outbox::find(const std::string& key) const {
    for (const auto& e : m_entries) if (e.key == key) return &e;
    return nullptr;
}

bool Outbox::begin(const std::string& key) {
    if (Entry* e = find(key)) {
        if (e->flight) return false;            // asked, not answered -- wait
        e->flight = true;
        return true;
    }
    m_entries.push_back(Entry{key, 0, true});
    return true;
}

void Outbox::succeed(const std::string& key) {
    std::erase_if(m_entries, [&](const Entry& e) { return e.key == key; });
}

Outbox::After Outbox::fail(const std::string& key) {
    Entry* e = find(key);
    if (!e) return After::Retry;                // never asked; nothing spent
    e->flight = false;
    ++e->tries;
    if (e->tries < kDeliveryTries) return After::Retry;
    // Spent. The entry goes, because the caller is about to put the key in the
    // announced set -- two records of the same "stop asking" would be one
    // record too many, and the announced set is the one that survives a restart.
    std::erase_if(m_entries, [&](const Entry& x) { return x.key == key; });
    return After::GiveUp;
}

void Outbox::prune(const std::vector<std::string>& live) {
    std::erase_if(m_entries, [&](const Entry& e) {
        return std::find(live.begin(), live.end(), e.key) == live.end();
    });
}

bool Outbox::in_flight(const std::string& key) const {
    const Entry* e = find(key);
    return e && e->flight;
}

int Outbox::tries(const std::string& key) const {
    const Entry* e = find(key);
    return e ? e->tries : 0;
}

std::size_t Outbox::size() const { return m_entries.size(); }

}  // namespace jot::core
