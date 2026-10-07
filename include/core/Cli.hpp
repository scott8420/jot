#pragma once
// core/Cli -- jot's command line, the part GLib cannot do (s061e).
//
// GLib's option parser takes the flags out of argv and keeps only "was it
// there", so `jot Groceries -a for the party -l milk eggs` reaches the app as
// "append, list, Groceries for the party milk eggs" -- which words were the
// line and which the items is gone. This reads argv BEFORE GLib does and, when
// it asks for both, rewrites it to one unambiguous form:
//
//   jot NAME -al "a line" item...        ->  jot --both NAME "a line" item...
//   jot NAME -a words... -l item...      ->  jot --both NAME "words..." item...
//   jot NAME -l item... -a words...      ->  (the same)
//   jot NAME -al "" item...              ->  jot NAME --list item...   (s061f)
//   jot NAME -al "a line"                ->  jot NAME --append "a line"
//
// Anything else comes back unchanged, for GLib and the app to read as before.

#include <string>
#include <vector>

namespace jot::core {

std::vector<std::string> cli_join_append_list(const std::vector<std::string>& argv);

}  // namespace jot::core
