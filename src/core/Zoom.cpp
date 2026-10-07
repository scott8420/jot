#include "core/Zoom.hpp"

#include <cstdlib>

namespace jot::core {

const std::vector<int>& zoom_steps() {
    static const std::vector<int> s{70, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300};
    return s;
}

int zoom_clamp(int pct) {
    const auto& s = zoom_steps();
    if (pct <= 0) return kZoomDefault;
    int best = s.front();
    for (int v : s)
        if (std::abs(v - pct) < std::abs(best - pct)) best = v;
    return best;
}

int zoom_in(int pct) {
    const int at = zoom_clamp(pct);
    for (int v : zoom_steps())
        if (v > at) return v;
    return zoom_steps().back();
}

int zoom_out(int pct) {
    const int at = zoom_clamp(pct);
    const auto& s = zoom_steps();
    for (auto it = s.rbegin(); it != s.rend(); ++it)
        if (*it < at) return *it;
    return s.front();
}

std::string zoom_text(int pct) { return std::to_string(zoom_clamp(pct)) + "%"; }

}  // namespace jot::core
