#include "Log.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <array>
#include <string>

namespace jot::log {

namespace {
// MUST match the number of entries in log::Area. Hand-kept, and a new area
// added without bumping it is a logger that never gets built -- s009 added
// Desktop and this line is the second half of that edit.
constexpr int kAreaCount = 10;

const char* area_name(Area a) {
    switch (a) {
        case Area::App:      return "app";
        case Area::Shell:    return "shell";
        case Area::Registry: return "registry";
        case Area::Recents:  return "recents";
        case Area::Io:       return "io";
        case Area::Model:    return "model";
        case Area::Tree:     return "tree";
        case Area::Editor:   return "editor";
        case Area::Drawer:   return "drawer";
        // s009: the one-way projection into the desktop. Its own area because a
        // sync crosses a process boundary and the only way to see what the
        // other side did is to have said what we sent.
        case Area::Desktop:  return "desktop";
    }
    return "?";
}

std::array<std::shared_ptr<spdlog::logger>, kAreaCount>& loggers() {
    static std::array<std::shared_ptr<spdlog::logger>, kAreaCount> a{};
    return a;
}
}  // namespace

void init() {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto& a = loggers();
    for (int i = 0; i < kAreaCount; ++i) {
        const auto area = static_cast<Area>(i);
        auto lg = std::make_shared<spdlog::logger>(area_name(area), sink);
        lg->set_level(spdlog::level::info);   // default INFO; raise one area to TRACE to debug
        a[static_cast<std::size_t>(i)] = lg;
    }
}

std::shared_ptr<spdlog::logger> get(Area area) {
    return loggers()[static_cast<std::size_t>(area)];
}

void set_level(Area area, spdlog::level::level_enum level) {
    if (auto lg = get(area)) lg->set_level(level);
}

void apply_debug_env(const char* value) {
    if (!value || !*value) return;

    const std::string spec = value;
    std::size_t i = 0;
    while (i <= spec.size()) {
        const std::size_t comma = spec.find(',', i);
        std::string item = spec.substr(i, comma == std::string::npos ? std::string::npos
                                                                     : comma - i);
        i = (comma == std::string::npos) ? spec.size() + 1 : comma + 1;

        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (item.empty()) continue;

        auto level = spdlog::level::debug;
        if (item.rfind("trace:", 0) == 0) {
            level = spdlog::level::trace;
            item = item.substr(6);
        }

        if (item == "all") {
            for (int a = 0; a < kAreaCount; ++a) set_level(static_cast<Area>(a), level);
            continue;
        }

        bool matched = false;
        for (int a = 0; a < kAreaCount; ++a) {
            const auto area = static_cast<Area>(a);
            if (item == area_name(area)) {
                set_level(area, level);
                matched = true;
                break;
            }
        }
        if (!matched)
            if (auto lg = get(Area::App))
                lg->warn("JOT_DEBUG: no log area called '{}'", item);
    }
}

}  // namespace jot::log
