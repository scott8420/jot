#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install-extension.sh -- put the Jot tile in GNOME's quick settings panel.
#
# The tile runs `jot --capture "text"`, and finds jot through the desktop entry
# install-desktop.sh writes -- so that runs first if it has not.
#
# Copies extension/jot-capture@scott8420.github.io into
# ~/.local/share/gnome-shell/extensions and enables it. On Wayland GNOME Shell
# only sees a NEW extension after you log out and back in; after that, re-running
# this to update it ALSO needs a log out / in: the shell caches the old code.
#
# `--uninstall` disables it and takes it back out.
# ─────────────────────────────────────────────────────────────────────────────
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

UUID="jot-capture@scott8420.github.io"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
DEST="$DATA/gnome-shell/extensions/$UUID"

if [ "${1:-}" = "--uninstall" ]; then
    gnome-extensions disable "$UUID" 2>/dev/null || true
    rm -rf "$DEST"
    echo "Removed the Jot tile. Log out and in to clear it from the shell."
    exit 0
fi

if [ ! -f "$DATA/applications/io.github.scott8420.Jot.desktop" ]; then
    echo "jot's desktop entry is not installed -- running install-desktop.sh first."
    ./install-desktop.sh
    echo ""
fi

NEW=1
[ -d "$DEST" ] && NEW=0
mkdir -p "$DEST"
cp -r "extension/$UUID/." "$DEST/"
echo "Installed: $DEST"

# Enabled by gsettings, not `gnome-extensions enable`: on Wayland the shell
# does not know a brand-new extension until the next login, and the CLI refuses
# one it does not know. The key is what the shell reads at login.
enable_at_login() {
    command -v gsettings >/dev/null || return 0
    local cur
    cur="$(gsettings get org.gnome.shell enabled-extensions)"
    case "$cur" in *"'$UUID'"*) return 0 ;; esac
    if [ "$cur" = "@as []" ] || [ "$cur" = "[]" ]; then
        gsettings set org.gnome.shell enabled-extensions "['$UUID']"
    else
        gsettings set org.gnome.shell enabled-extensions "${cur%]}, '$UUID']"
    fi
}
enable_at_login

if [ "$NEW" = "1" ]; then
    echo ""
    echo "First install: log out and back in, then the Jot tile is in quick settings."
    echo "(If it is not, run: gnome-extensions enable $UUID)"
else
    echo "Updated. Log out and back in to load the new code."
fi
