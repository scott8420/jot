#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install-desktop.sh -- make jot INSTALLED enough to be notified from.
#
# Run once. `./build.sh --install-desktop` calls this.
#
# ── why a notification needs this at all ──────────────────────────────────────
# GNOME's notification daemon routes by APPLICATION ID: it looks the sending
# application up among the installed desktop entries, and one it cannot find is
# dropped, silently, with no error anywhere. jot runs out of a build tree, so
# without this the feature's failure mode is "works perfectly, shows nothing" --
# indistinguishable from a bug. jot checks for the entry at startup and says so
# in the Today footer rather than leaving you to guess.
#
# Three files, and each earns its place:
#
#   the .desktop entry  -- the lookup the daemon performs, and what puts jot in
#                          Settings > Notifications where it can be turned off
#                          the way every other application can.
#   the icon            -- jot's logo is compiled into the binary as a GResource,
#                          which is the RENDERING PROCESS's problem: the Shell
#                          draws the notification and has no access to jot's
#                          resources. An icon referenced by name has to be a file
#                          on disk or it falls back to a generic square.
#   the D-Bus service   -- so that clicking a notification after jot has been
#                          closed STARTS it and then delivers the action, rather
#                          than doing nothing. DBusActivatable without this is
#                          worse than neither: GNOME stops falling back to Exec.
#
# Everything goes under ~/.local/share -- nothing is written outside your home
# and nothing needs root. `--uninstall` takes all three back out again.
# ─────────────────────────────────────────────────────────────────────────────
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

APP_ID="io.github.scott8420.Jot"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS="$DATA/applications"
ICONS="$DATA/icons/hicolor/symbolic/apps"
SERVICES="$DATA/dbus-1/services"

if [ "${1:-}" = "--uninstall" ]; then
    rm -f "$APPS/$APP_ID.desktop" "$ICONS/jot-logo-symbolic.svg" \
          "$SERVICES/$APP_ID.service"
    command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" || true
    echo "Removed jot's desktop entry, icon and D-Bus service."
    exit 0
fi

JOT_EXEC="$(pwd)/build/jot"
if [ ! -x "$JOT_EXEC" ]; then
    echo "No binary at $JOT_EXEC -- run ./build.sh first." >&2
    exit 1
fi

mkdir -p "$APPS" "$ICONS" "$SERVICES"

# The absolute path of the build tree is baked in ON PURPOSE. jot is not
# installed to /usr/bin and pretending otherwise would give a desktop entry that
# launches nothing. Move or rebuild the tree elsewhere and re-run this.
sed "s|@JOT_EXEC@|$JOT_EXEC|g" resources/$APP_ID.desktop.in > "$APPS/$APP_ID.desktop"
sed "s|@JOT_EXEC@|$JOT_EXEC|g" resources/$APP_ID.service.in > "$SERVICES/$APP_ID.service"
cp resources/icons/scalable/apps/jot-logo-symbolic.svg "$ICONS/jot-logo-symbolic.svg"

command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" || true
command -v gtk4-update-icon-cache  >/dev/null && gtk4-update-icon-cache -qtf "$DATA/icons/hicolor" || true

echo "Installed:"
echo "  $APPS/$APP_ID.desktop"
echo "  $ICONS/jot-logo-symbolic.svg"
echo "  $SERVICES/$APP_ID.service"
echo ""
echo "Exec points at $JOT_EXEC -- re-run this if the tree moves."
echo "Restart jot; the Today footer should stop saying it is not installed."
