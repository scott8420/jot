#pragma once
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Zoom (s053b) -- how big the NOTE's text is, for eyes that need it
// bigger (Scott: "zoom in/out to allow hard-to-see users"). A percent of the
// theme's text size, on fixed steps -- the browser / Preview way, so Ctrl+=
// then Ctrl+- always lands back where it started. Only the note (Source, Live
// Preview, Reading) zooms; the panes around it keep the desktop's size, which
// GNOME's own Large Text setting already covers.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot::core {

inline constexpr int kZoomDefault = 100;

const std::vector<int>& zoom_steps();     // 70 ... 300
int  zoom_clamp(int pct);                 // the nearest step; junk -> 100
int  zoom_in(int pct);                    // the next step up (stays at the top)
int  zoom_out(int pct);                   // the next step down (stays at the bottom)
std::string zoom_text(int pct);           // "125%"

}  // namespace jot::core
