#include "core/Nudge.hpp"
#include "core/Markdown.hpp"

#include <algorithm>
#include <ctime>
#include <functional>

namespace jot::core {
namespace {

// Days from 1970-01-01 to y-m-d (proleptic Gregorian; Howard Hinnant).
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

std::tm local(std::int64_t when) {
    std::tm tm{};
    const std::time_t t = static_cast<std::time_t>(when);
    localtime_r(&t, &tm);
    return tm;
}

bool contains(const std::vector<std::string>& v, const std::string& k) {
    return std::find(v.begin(), v.end(), k) != v.end();
}

NodeId nudge_node(std::string_view key) {
    if (!is_nudge_key(key)) return {};
    key.remove_prefix(std::string_view(kNudgePrefix).size());
    const auto at = key.rfind('@');
    return at == std::string_view::npos ? NodeId{} : NodeId(key.substr(0, at));
}

}  // namespace

std::int64_t local_day(std::int64_t now) {
    const std::tm tm = local(now);
    return days_from_civil(tm.tm_year + 1900, static_cast<unsigned>(tm.tm_mon + 1),
                           static_cast<unsigned>(tm.tm_mday));
}

std::string nudge_key(const NodeId& id, std::int64_t period) {
    return std::string(kNudgePrefix) + id + "@" + std::to_string(period);
}

bool is_nudge_key(std::string_view key) { return key.starts_with(kNudgePrefix); }

bool nudges_now(const Node& n, std::int64_t now) {
    if (!n.packet || n.nudge <= 0 || n.sent != 0) return false;
    const Task& t = n.task;
    if (t.is_task && t.done) return false;
    if (t.project == ProjectState::Completed || t.project == ProjectState::Dropped ||
        t.project == ProjectState::OnHold)
        return false;
    if (t.defer > now) return false;
    const PacketState st = packet_state(n.body);
    return st.total > 0 && !st.complete();
}

AnnounceResult packet_nudges(const NodeSource& src, std::int64_t now,
                             const std::vector<std::string>& announced,
                             const std::vector<Snoozed>& parked) {
    AnnounceResult r;
    const bool hour_ok = local(now).tm_hour >= kNudgeHour;
    const std::int64_t today = local_day(now);
    std::function<void(const NodeId&)> walk = [&](const NodeId& parent) {
        for (const auto& id : src.children(parent)) {
            const Node* n = src.find(id);
            if (!n) continue;
            if (nudges_now(*n, now)) {
                // "In 3 days" silences the PACKET, not one period: a park from
                // any period still ahead holds it, and stays live so the prune
                // keeps it until it wakes.
                const std::string mine = std::string(kNudgePrefix) + id + "@";
                const auto held = std::find_if(parked.begin(), parked.end(), [&](const Snoozed& z) {
                    return z.key.starts_with(mine) && z.until > now;
                });
                if (held != parked.end()) {
                    r.live.push_back(held->key);
                    walk(id);
                    continue;
                }
                const std::string key = nudge_key(id, today / n->nudge);
                r.live.push_back(key);
                if (contains(announced, key)) {
                    r.keep.push_back(key);
                } else if (hour_ok && !is_parked(parked, key, now)) {
                    const PacketState st = packet_state(n->body);
                    Announcement a;
                    a.id      = id;
                    a.key     = key;
                    a.summary = nudge_title(n->title, st);
                    a.detail  = nudge_detail(st);
                    r.to_show.push_back(std::move(a));
                }
            }
            walk(id);
        }
    };
    walk("");
    return r;
}

std::string nudge_title(const std::string& title, const PacketState& st) {
    return (title.empty() ? std::string("Untitled") : title) + ": " + std::to_string(st.in) + " of " +
           std::to_string(st.total) + " in";
}

std::string nudge_detail(const PacketState& st) {
    const auto miss = st.missing();
    if (miss.empty()) return {};
    std::string out = "Missing: ";
    for (std::size_t i = 0; i < miss.size() && i < 3; ++i) out += (i ? ", " : "") + miss[i];
    if (miss.size() > 3) out += " and " + std::to_string(miss.size() - 3) + " more";
    return out;
}

std::vector<NoticeButton> nudge_buttons(const std::string& key, const PacketState& st) {
    std::vector<NoticeButton> out{{"In 3 days", {NoticeVerb::Remind, 3, key}}};
    if (st.total - st.in == 1)
        for (const auto& it : st.items)
            if (!it.in()) out.push_back({"I’ve got it", {NoticeVerb::Got, it.line, key}});
    return out;
}

NudgeState check_nudge_target(const NodeSource& src, std::string_view key) {
    const NodeId id = nudge_node(key);
    const Node* n = id.empty() ? nullptr : src.find(id);
    if (!n) return NudgeState::Gone;
    if (!n->packet) return NudgeState::NotPacket;
    if (n->sent != 0) return NudgeState::Sent;
    const PacketState st = packet_state(n->body);
    if (st.complete()) return NudgeState::AllIn;
    return NudgeState::Current;
}

std::string nudge_state_words(NudgeState s) {
    switch (s) {
    case NudgeState::Current:   return {};
    case NudgeState::Gone:      return "it is not in this jots folder any more";
    case NudgeState::NotPacket: return "it is not a packet any more";
    case NudgeState::AllIn:     return "everything is in now";
    case NudgeState::Sent:      return "it has been sent";
    }
    return {};
}

std::string tick_line(const std::string& body, int line) {
    const Scan sc = scan(body);
    if (line < 0 || line >= static_cast<int>(sc.lines.size())) return {};
    const Line& ln = sc.lines[static_cast<std::size_t>(line)];
    if (ln.block != Block::Task || ln.checked || ln.box_begin < 0 || ln.box_end - ln.box_begin != 3)
        return {};
    std::string out = body;
    out[static_cast<std::size_t>(ln.box_begin) + 1] = 'x';
    return out;
}

std::string nudge_every_text(int days) {
    switch (days) {
    case 0:  return "Never";
    case 1:  return "Every day";
    case 7:  return "Every week";
    case 14: return "Every 2 weeks";
    case 30: return "Every month";
    default: return "Every " + std::to_string(days) + " days";
    }
}

const std::vector<int>& nudge_choices() {
    static const std::vector<int> c{0, 1, 3, 7, 14, 30};
    return c;
}

}  // namespace jot::core
