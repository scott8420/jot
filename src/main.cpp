#include "App.hpp"
#include "core/Cli.hpp"
#include "core/Glance.hpp"

#include <glib.h>
#include <ctime>
#include <iostream>

#include <string>
#include <unistd.h>
#include <vector>

// Thin bootstrap (CANON). Everything real is in App / Shell / the core.
// s061e: argv goes through core::cli_join_append_list first -- GLib throws
// away which words followed -a and which -l, so a both-at-once call is
// rewritten to `--both NAME "line" items...` before GLib sees it.
int main(int argc, char** argv) {
    std::vector<std::string> in(argv, argv + argc);
    // s071: `jot --glance [ics]` -- the day on one card, printed. Answered here,
    // before GTK, so a cron job with no display and no session can mail it.
    // It reads the jots folder on disk, running jot or not.
    {
        std::string text, err;
        int grc = 0;
        if (jot::core::glance_command(in, g_get_user_data_dir(),
                                      static_cast<std::int64_t>(std::time(nullptr)),
                                      text, err, grc)) {
            std::cout << text;
            std::cerr << err;
            return grc;
        }
    }
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
