#pragma once
#include "core/Nodes.hpp"
#include "core/Tasks.hpp"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Glance -- s066: the day on one card, to take away.
//
// Scott, Oct 7: "a 'Glance of Today' view that is the day's activities. A
// quick listing that could be emailed to my iphone as a note or calendar
// event." His phone does not sync with this machine; he reaches it through
// Gmail, Keep and the iCloud website. So the Glance is a SMALL, PORTABLE
// reading of the day, in three renderings of one list:
//
//   glance_text  plain text -- Copy (Keep, Notes, anywhere) and Email's body
//   glance_html  the same, formatted -- Copy's rich flavour (iCloud Notes,
//                Gmail's compose keep the headings and bullets)
//   glance_ics   an iCalendar file -- one all-day event "jot — Wed 7 Oct"
//                carrying the list, plus a timed event for each thing due at
//                a real hour today. Google Calendar's Import, the iPhone's
//                Mail ("Add to Calendar") and the Mac's Calendar all take it.
//
// WHAT IS ON IT, in this order, and EACH THING ONCE (the first section it
// fits) -- a card you read on a phone must not say the same thing twice:
//
//   Late           todos past their due (effective due < now), not done
//   Due today      effective due between now and tonight
//   Running short  the next step of a deadline running short (s055)
//   Starts today   a defer that runs out today (it becomes doable)
//   Flagged        flagged (or in a flagged project), not done
//   Errands        what is doable at a place (#at/...), by place (s056)
//
// A thing listed earlier that is also an errand says its place ("at Town").
// Done and dropped work never appears. Nothing here writes, and nothing is
// cached: a Glance is a reading of the model at `now`, like Today itself.
//
// GTK-free; the selftest owns every verdict and every byte of the renderings.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

enum class GlanceKind { Late, DueToday, Short, Starts, Flagged, Errands };
const char* glance_head(GlanceKind k);   // "Late", "Due today", ...

struct GlanceItem {
    NodeId      id;
    std::string title;
    std::string detail;            // "2 days late · Car · ~15m · at Town"
    std::int64_t due = 0;          // effective due (0 = none) -- the ics needs it
    int          minutes = 0;      // the estimate
};

struct GlanceSection {
    GlanceKind              kind = GlanceKind::Late;
    std::string             head;  // glance_head(kind), or the place for Errands
    std::vector<GlanceItem> items;
};

struct Glance {
    std::int64_t               day = 0;      // 00:00 local of the day it reads
    std::string                title;        // "jot — Wed 7 Oct"
    std::string                summary;      // "6 things · ~1h 20m · 1 late"
    std::vector<GlanceSection> sections;     // only non-empty ones, in the order above
    int count() const;                       // things listed (each once)
};

Glance glance(const NodeSource& src, const TaskIndex& tasks, std::int64_t now);

std::string glance_text(const Glance& g);
std::string glance_html(const Glance& g);
// `stamp` is DTSTAMP (now, in practice); a parameter so the bytes are testable.
std::string glance_ics(const Glance& g, std::int64_t stamp);
// "jot-glance-2026-10-07.ics"
std::string glance_ics_name(const Glance& g);
// mailto:<to>?subject=...&body=... (percent-encoded; CRLF line breaks). `to`
// may be empty -- the mail app then asks.
std::string glance_mailto(const Glance& g, const std::string& to);

// The iCalendar TEXT escape (RFC 5545 3.3.11) and line folding (3.1) -- public
// for the selftest.
std::string ics_escape(const std::string& s);
std::string ics_fold(const std::string& line);

}  // namespace jot::core
