#!/usr/bin/env bash
# jot build. Deps (Ubuntu/Debian): libgtkmm-4.0-dev libspdlog-dev cmake g++.
# Optional, for the desktop projection: libecal2.0-dev libedataserver1.2-dev
# (Fedora: evolution-data-server-devel). cmake says which way it went.
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"
cmake -B build
cmake --build build -- -j"$(nproc)"
# Both halves of the desktop #ifdef, not just the one this machine selects.
# A branch nothing compiles is a branch that is broken and does not know it --
# s009 shipped exactly that. Cheap: one extra target, no extra dependency.
if [ "${JOT_BOTH:-0}" = "1" ]; then
    cmake -B build-noecal -DJOT_DESKTOP=OFF
    cmake --build build-noecal -- -j"$(nproc)"
    echo "Also built: ./build-noecal/jot (the no-libecal path)"
fi

# A due notification is DROPPED SILENTLY unless GNOME can look jot's
# application id up among the installed desktop entries. Opt-in and explicit:
# a build script that writes into your home without being asked is not one.
if [ "${1:-}" = "--install-desktop" ]; then
    ./install-desktop.sh
fi

echo ""
echo "Built: ./build/jot  and  ./build/jot_selftest"
echo "Smoke: ./build/jot_selftest"
echo "Notifications: ./build.sh --install-desktop   (once; see install-desktop.sh)"
