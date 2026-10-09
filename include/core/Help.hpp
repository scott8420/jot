#pragma once
// core::Help -- the GUIDE (s067, J6).
//
// Scott, Oct: jot "needs in-app help to guide users in getting the most out of
// it". The cheat sheet (s030) already says every KEY, one line each; what it
// cannot say is what the parts are FOR -- why the Inbox is a mark and not a
// place, why a todo is a note, what "done when" buys you. The guide says that:
// a short page per idea, in the order someone meets them.
//
// Each page can also DO something: a "Try it" button runs the verb it is about
// (open the Inbox, show the timeline), and "Every key for this" opens the cheat
// sheet already filtered to the page's lines. So the guide teaches by pointing
// at the real thing, and never spells a key by hand -- a Try's key is read from
// core::shortcut_registry(), like the cheat sheet's.
//
// Stones, as failing tests rather than things to remember:
//   - every Try names a verb jot really has (the registry, or the few bound
//     unkeyed ones in help_bound_actions());
//   - every See also names a page that exists;
//   - every page's cheat query finds at least one cheat-sheet line;
//   - the light marks (**bold**, *italic*, `code`) become safe Pango markup.
//
// GTK-free, so all of that is proved headless.
#include <string>
#include <vector>

namespace jot::core {

// A button on a page that does the thing the page describes.
struct HelpTry {
    std::string label;     // "Open the Inbox"
    std::string action;    // a DETAILED action name: "win.timeline", "win.left-view::inbox"
};

struct HelpTopic {
    std::string id;                   // "inbox" -- stable; See also and the window use it
    std::string group;                // one of help_groups(), contiguous, in that order
    std::string title;                // "The Inbox"
    std::string icon;                 // a themed / jot icon name
    std::string lead;                 // one sentence under the title: the idea
    std::vector<std::string> body;    // paragraphs; "- " starts a bullet; **b** *i* `code`
    std::vector<HelpTry> tries;       // may be empty
    std::string keys;                 // a cheat-sheet query: "Every key for this"
    std::vector<std::string> see;     // other topic ids
};

// The sidebar's headings, in reading order.
const std::vector<std::string>& help_groups();

// Every page, grouped by help_groups() order. The first is the welcome.
const std::vector<HelpTopic>& help_topics();

// The page with this id, or nullptr.
const HelpTopic* help_topic(const std::string& id);

// Where a page sits in help_topics(), -1 if unknown. Previous / Next walk it.
int help_index(const std::string& id);

// True when every word of `query` (trimmed, ASCII case-blind) appears in the
// page's title, lead, body or group. Empty matches everything.
bool help_matches(const HelpTopic& t, const std::string& query);

// "**Inbox** is a `mark`" -> "<b>Inbox</b> is a <tt>mark</tt>": escapes & < > ' "
// first, then the three light marks. An unclosed mark is closed at the end, so
// the result always parses. Inside `code` nothing else is read.
std::string help_markup(const std::string& text);

// "win.left-view::inbox" -> name "win.left-view", target "inbox".
// A plain name has an empty target.
void help_split_action(const std::string& detailed, std::string& name, std::string& target);

// The key a Try's verb has in the shortcut registry ("Ctrl+Shift+L"), or "".
std::string help_try_keys(const HelpTry& t);

// Verbs jot binds that have no key, so the registry does not list them, and a
// Try may still name. Kept short on purpose.
const std::vector<std::string>& help_bound_actions();

// s071: the "?" in a view -- the guide page for what is on screen. `view` is
// a left-pane tab ("notes", "inbox", "today", "tags", "projects"), a Today
// sub-view ("available", "flagged", "logbook", "forecast", "errands"), "find"
// (Find has words), or a window of its own ("timeline", "glance").
// Unknown -> "welcome", so a "?" always opens somewhere.
std::string help_page_for(const std::string& view);
// Every view name help_page_for knows -- the selftest walks them.
const std::vector<std::string>& help_views();

// The stones, as data -- each empty when all is well.
std::vector<std::string> help_unknown_actions();   // Tries naming no verb jot has
std::vector<std::string> help_bad_links();         // See also naming no page
std::vector<std::string> help_empty_keys();        // pages whose cheat query finds nothing

}  // namespace jot::core
