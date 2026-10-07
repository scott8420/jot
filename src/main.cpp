#include "App.hpp"
#include "core/Cli.hpp"

#include <string>
#include <unistd.h>
#include <vector>

// Thin bootstrap (CANON). Everything real is in App / Shell / the core.
// s061e: argv goes through core::cli_join_append_list first -- GLib throws
// away which words followed -a and which -l, so a both-at-once call is
// rewritten to `--both NAME "line" items...` before GLib sees it.
int main(int argc, char** argv) {
    std::vector<std::string> in(argv, argv + argc);
    std::vector<std::string> out = jot::core::cli_join_append_list(in);
    std::vector<char*> raw;
    raw.reserve(out.size() + 1);
    for (auto& a : out) raw.push_back(a.data());
    raw.push_back(nullptr);
    std::string restart;
    int rc = 0;
    {
        auto app = jot::App::create();
        rc = app->run(static_cast<int>(out.size()), raw.data());
        restart = app->restart_path();
    }
    // s062e: an older build quit so the new one could run. exec in this same
    // process, the bus name already released, so it comes up as THE jot.
    if (!restart.empty()) {
        char* const args[] = {restart.data(), nullptr};
        execv(restart.c_str(), args);
        return 1;   // exec failed; the old jot has already saved and gone
    }
    return rc;
}
