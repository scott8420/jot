#include "core/NoticeAction.hpp"
#include "core/Tasks.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>

namespace jot::core {
namespace {

const char* verb_word(NoticeVerb v) {
    switch (v) {
    case NoticeVerb::Done:   return "done";
    case NoticeVerb::Snooze: return "snooze";
    case NoticeVerb::Remind: return "remind";
    }
    return "done";
}

std::optional<NoticeVerb> verb_of(std::string_view w) {
    if (w == "done")   return NoticeVerb::Done;
    if (w == "snooze") return NoticeVerb::Snooze;
    if (w == "remind") return NoticeVerb::Remind;
    return std::nullopt;
}

}  // namespace

std::string encode_notice_act(const NoticeAct& a) {
    return std::string(verb_word(a.verb)) + ":" + std::to_string(a.amount) + ":" + a.key;
}

std::optional<NoticeAct> decode_notice_act(std::string_view s) {
    const auto c1 = s.find(':');
    if (c1 == std::string_view::npos) return std::nullopt;
    const auto c2 = s.find(':', c1 + 1);
    if (c2 == std::string_view::npos) return std::nullopt;

    const auto verb = verb_of(s.substr(0, c1));
    if (!verb) return std::nullopt;

    const std::string_view num = s.substr(c1 + 1, c2 - c1 - 1);
    int amount = 0;
    const auto [p, ec] = std::from_chars(num.data(), num.data() + num.size(), amount);
    if (ec != std::errc{} || p != num.data() + num.size() || num.empty()) return std::nullopt;

    NoticeAct a;
    a.verb   = *verb;
    a.amount = amount;
    a.key    = std::string(s.substr(c2 + 1));
    // A key is "<id>@<due>": an empty id or a missing '@' is not one.
    const auto at = a.key.rfind('@');
    if (at == std::string::npos || at == 0) return std::nullopt;
    // A snooze of nothing, or of a negative time, would say it again at once --
    // indistinguishable from a button that does not work. Refuse it here.
    if (a.verb != NoticeVerb::Done && a.amount <= 0) return std::nullopt;
    return a;
}

NodeId key_node(std::string_view key) {
    const auto at = key.rfind('@');
    if (at == std::string_view::npos) return {};
    return NodeId(key.substr(0, at));
}

std::vector<NoticeButton> todo_notice_buttons(const Announcement& a) {
    return {
        {"Mark done", {NoticeVerb::Done,   0,  a.key}},
        {"In 1 hour", {NoticeVerb::Snooze, 60, a.key}},
        {"Tomorrow",  {NoticeVerb::Remind, 1,  a.key}},
    };
}

TargetState check_notice_target(const NodeSource& src, std::string_view key) {
    const NodeId id = key_node(key);
    const Node* n = id.empty() ? nullptr : src.find(id);
    if (!n) return TargetState::Gone;
    if (!n->task.is_task) return TargetState::NotTodo;
    if (n->task.done) return TargetState::AlreadyDone;
    // The same rule that MADE the key, asked again now. A repeat that rolled
    // on, or a date moved by hand, gives a different key -- and that notice is
    // about an occurrence that no longer exists.
    if (announce_key(id, effective_due(src, id)) != key) return TargetState::Moved;
    return TargetState::Current;
}

std::string target_state_words(TargetState s) {
    switch (s) {
    case TargetState::Current:     return {};
    case TargetState::Gone:        return "that todo is not in this jots folder any more";
    case TargetState::NotTodo:     return "that note is not a todo any more";
    case TargetState::AlreadyDone: return "it was already done";
    case TargetState::Moved:       return "its due date has changed since the notice was sent";
    }
    return {};
}

std::int64_t notice_until(const NoticeAct& a, std::int64_t now) {
    switch (a.verb) {
    case NoticeVerb::Done:   return now;
    case NoticeVerb::Snooze: return now + static_cast<std::int64_t>(a.amount) * 60;
    case NoticeVerb::Remind: {
        // Calendar days: step the DATE and let mktime find the instant, so the
        // clock reads the same tomorrow even across a DST change.
        std::time_t t = static_cast<std::time_t>(now);
        std::tm lt{};
        localtime_r(&t, &lt);
        lt.tm_mday += a.amount;
        lt.tm_isdst = -1;
        return static_cast<std::int64_t>(std::mktime(&lt));
    }
    }
    return now;
}

void park(std::vector<Snoozed>& ledger, std::vector<std::string>& announced,
          const std::string& key, std::int64_t until) {
    std::erase_if(ledger, [&](const Snoozed& z) { return z.key == key; });
    ledger.push_back(Snoozed{key, until});
    std::erase(announced, key);
}

bool is_parked(const std::vector<Snoozed>& ledger, const std::string& key, std::int64_t now) {
    return std::any_of(ledger.begin(), ledger.end(),
                       [&](const Snoozed& z) { return z.key == key && z.until > now; });
}

bool prune_parked(std::vector<Snoozed>& ledger, const std::vector<std::string>& live,
                  std::int64_t now) {
    const auto before = ledger.size();
    std::erase_if(ledger, [&](const Snoozed& z) {
        return z.until <= now ||
               std::find(live.begin(), live.end(), z.key) == live.end();
    });
    return ledger.size() != before;
}

}  // namespace jot::core
