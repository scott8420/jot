#pragma once
#include "core/Nodes.hpp"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Packet (s044, J2) -- a piece of work that is a SET OF REQUIRED THINGS.
//
// Scott's taxes: "I contact the accountant in Jan, then gather tax papers
// incrementally until all are sent." A packet is a note marked as one
// (Node::packet); its CHECKBOX LINES are the required items:
//
//     - [ ] W-2 (employer)       [w2-2026.pdf](attachments/w2-2026.pdf)
//     - [ ] 1099-INT (bank)
//     - [x] Property tax receipt          <- paper copy, ticked by hand
//
// ── when is an item IN ─────────────────────────────────────────────────────
// When its line carries a file -- an attachment or a linked file, dropped
// onto that line -- OR it is ticked. The file is the normal answer ("one
// place, one time": the document lives with the work it is for); the tick is
// "I've got it" for the thing that only exists on paper. Either way the note
// says so in its own text, so there is no second store to disagree with it
// (the s035b rule: store it where it is portable, read it where it is found).
//
// ── what it is NOT (yet) ───────────────────────────────────────────────────
// Gathering for sending, the nudges and coming back next year are later
// milestones; they all read this.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

struct PacketItem {
    std::string              label;   // the line's words, links taken out
    int                      line = 0;
    bool                     ticked = false;
    std::vector<std::string> files;   // attachment names / linked file:// keys on the line
    bool in() const { return ticked || !files.empty(); }
};

struct PacketState {
    std::vector<PacketItem> items;
    int in    = 0;
    int total = 0;
    bool complete() const { return total > 0 && in == total; }
    std::vector<std::string> missing() const;   // labels, in order
};

PacketState packet_state(const std::string& body);

// "4 of 5 in  ·  missing: 1099-INT (bank)" / "All 5 in" / "No items yet --
// add a checkbox line for each thing it needs". The missing list is cut after
// three ("and 2 more").
std::string packet_line(const PacketState& s);

// s045: just the counts, for where the items are listed anyway (Note
// details): "3 of 5 in  ·  2 missing" / "All 5 in" / the no-items hint.
std::string packet_count(const PacketState& s);

// ── s053: comes back next year ─────────────────────────────────────────────
// "It comes back next year with the same list and empty slots." DO IT AGAIN
// makes a FRESH COPY beside a packet and leaves the old one as the record --
// its files, its ticks, its Sent stamp -- because last year's papers are
// exactly what you look at when gathering this year's.

// "Taxes 2026" -> "Taxes 2027" (the LAST standalone 19xx / 20xx moves on one);
// with no year, "<title> (next)".
std::string next_title(const std::string& title);

// The items with nothing in: every task line unticked and every FILE link on
// it taken out (web links stay -- they are where to get it, not the thing).
// Other lines are kept. A trailing "Last time: ..." line of an earlier round
// is dropped, so the chain does not pile up.
std::string fresh_body(const std::string& body);

// The copy, one place below `id` among its siblings: next_title, fresh_body +
// "Last time: [<old title>](jot:<id>) -- sent <date>.", a packet with the same
// nudge, and -- when it is a todo -- the same task record not done, its due
// and defer one calendar year on. Returns the new id, or empty when `id` is
// not a packet. Several writes: the caller makes them one step.
NodeId packet_again(NodeSource& src, const NodeId& id);

}  // namespace jot::core
