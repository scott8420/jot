#pragma once
#include "core/NoticeAction.hpp"
#include "core/Nodes.hpp"
#include "core/Notify.hpp"
#include "core/Packet.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Nudge (s052, J2) -- a packet that is NOT all in speaks up.
//
// Scott: "flagged when one is missing with an offer to remind him to look for
// it". A packet can be told to nudge every N days (Node::nudge). While it has
// items missing, is not sent, not done and not deferred, it says once per
// period -- on the first minute tick after 9:00 --
//
//     Taxes 2026: 3 of 4 in
//     Missing: 1099-INT (bank)                [In 3 days] [I've got it] [Open]
//
// It rides s041's machinery whole: the same announced set, snooze ledger,
// outbox and `app.notice` action. What is new is a KEY SHAPE --
// "nudge:<id>@<period>" -- where period = local day number / N. A new period is
// a new key, so it says again; the same period stays quiet once delivered.
// "I've got it" is offered only when exactly ONE item is missing (with several,
// which one would it tick?) and ticks that item's line: the paper-copy answer.
//
// Pure: no GTK, no clock of its own.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

inline constexpr const char* kNudgePrefix = "nudge:";
inline constexpr int         kNudgeHour   = 9;   // not before 9:00 local

// Days since 1970-01-01 on the LOCAL calendar (so a period turns at local
// midnight, DST or not).
std::int64_t local_day(std::int64_t now);

std::string nudge_key(const NodeId& id, std::int64_t period);
bool        is_nudge_key(std::string_view key);

// Does this packet nudge at all right now (marked, cadence set, has items,
// something missing, not sent, not done / dropped, not deferred past now)?
bool nudges_now(const Node& n, std::int64_t now);

// The packets' half of the announcer -- same shape and same rules as
// due_announcements, so the Shell merges the two (union of keep and of live).
AnnounceResult packet_nudges(const NodeSource& src, std::int64_t now,
                             const std::vector<std::string>& announced,
                             const std::vector<Snoozed>& parked = {});

// The notification's words: "Taxes 2026: 3 of 4 in", "Missing: a, b and 2 more".
std::string nudge_title(const std::string& title, const PacketState& st);
std::string nudge_detail(const PacketState& st);

// [In 3 days] always; [I've got it] when exactly one item is missing (its line
// rides as the amount). The Shell adds [Open] (goto-node, not a notice act).
std::vector<NoticeButton> nudge_buttons(const std::string& key, const PacketState& st);

// Is the key's packet still one that would nudge?
enum class NudgeState { Current, Gone, NotPacket, AllIn, Sent };
NudgeState  check_nudge_target(const NodeSource& src, std::string_view key);
std::string nudge_state_words(NudgeState s);

// The body with task line `line` ticked, or EMPTY if that line is not an
// unticked task line (a stale button). Only the box changes.
std::string tick_line(const std::string& body, int line);

// "Every day", "Every 3 days", "Every week", "Every 2 weeks", "Every month",
// "Every 10 days"; 0 -> "Never".
std::string nudge_every_text(int days);
// The choices the Packet section offers, in order (0 first).
const std::vector<int>& nudge_choices();

}  // namespace jot::core
