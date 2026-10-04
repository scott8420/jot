#pragma once
#include "core/Nodes.hpp"

#include <gtkmm/widget.h>

#include <cstdint>
#include <functional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// TaskCard (s042, J1) -- THE todo row. One builder, so Today, Forecast and Tags
// draw a todo the same way and cannot drift: a rounded card, a stripe on the
// left edge in the state's colour, a round tick ringed in it, the title with
// the due chip on the right, and a quiet line under it (project chip, why it
// is waiting, ⏱, ↻).
//
// What the card SAYS is core::row_look (pure, under the selftest). This file
// only lays it out and hangs the CSS classes on it; the colours are in
// Appearance.cpp, which knows whether the desktop is light or dark.
//
// The tick writes straight through the NodeSource and the card does not remove
// itself -- it waits for the model's refresh, as every row in jot does.
// ─────────────────────────────────────────────────────────────────────────────
namespace jot {

struct CardOpts {
    std::string prefix;            // widget-name prefix: "today", "tags"
    bool        show_project = true;   // off where a heading already names it
    std::string note;              // an extra word for the quiet line ("waiting for ...")
    bool        dim = false;       // a folded "done" list
};

Gtk::Widget* task_card(core::NodeSource& src, const core::Node& n, std::int64_t now,
                       const CardOpts& opts,
                       std::function<void(const core::NodeId&)> on_open);

}  // namespace jot
