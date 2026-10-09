#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install.sh -- install jot for your login. No sudo; nothing outside your home.
#
#   ./install.sh                  build, check, install; asks about Start at login
#   ./install.sh --autostart      ... and start jot at login (no question)
#   ./install.sh --no-autostart   ... and do not (removes it if it was on)
#   ./install.sh --no-tile        leave the Jot tile out of quick settings
#   ./install.sh -y               no questions: keeps Start at login as it was
#
# Run it again to update: it rebuilds, and every file is replaced in place.
# ./uninstall.sh takes all of it back out (never a jots folder).
#
# What goes where:
#   ~/.local/bin/jot                                   the program (a release build)
#   ~/.local/share/applications/<id>.desktop           the launcher / notifications
#   ~/.local/share/dbus-1/services/<id>.service        a notification click starts jot
#   ~/.local/share/icons/hicolor/symbolic/apps/...     the pixie, for the Shell to draw
#   ~/.local/share/gnome-shell/extensions/jot-capture@...   the Jot tile
#   ~/.config/autostart/<id>.desktop                   Start at login (if chosen)
#   the global capture hotkey, if you set one, is pointed at ~/.local/bin/jot
#
# Your notes are never touched: jots folders live wherever you made them.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
SRC="$(pwd)"

APP_ID="io.github.scott8420.Jot"
OBJ_PATH="/io/github/scott8420/Jot"
UUID="jot-capture@scott8420.github.io"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
CONF="${XDG_CONFIG_HOME:-$HOME/.config}"
BIN_DIR="$HOME/.local/bin"
BIN="$BIN_DIR/jot"
APPS="$DATA/applications"
SERVICES="$DATA/dbus-1/services"
ICONS="$DATA/icons/hicolor/symbolic/apps"
EXT_DIR="$DATA/gnome-shell/extensions/$UUID"
AUTOSTART="$CONF/autostart/$APP_ID.desktop"
MANIFEST="$DATA/jot/installed.txt"
SLOT="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/jot-capture/"
KB_SCHEMA="org.gnome.settings-daemon.plugins.media-keys"

AUTO="ask"; TILE=1; YES=0
for a in "$@"; do
    case "$a" in
        --autostart)    AUTO="on" ;;
        --no-autostart) AUTO="off" ;;
        --no-tile)      TILE=0 ;;
        -y|--yes)       YES=1 ;;
        -h|--help)      sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "install.sh: unknown option '$a' (try --help)" >&2; exit 2 ;;
    esac
done

say()  { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
PUT=()   # what this run put where, for the closing list

# ── 1. what the build needs ─────────────────────────────────────────────────
step "Checking what the build needs"
missing=()
command -v cmake      >/dev/null || missing+=("cmake")
command -v g++        >/dev/null || missing+=("g++")
command -v pkg-config >/dev/null || missing+=("pkg-config")
if command -v pkg-config >/dev/null; then
    pkg-config --exists gtkmm-4.0 || missing+=("gtkmm 4")
    pkg-config --exists spdlog    || missing+=("spdlog")
fi
if [ ${#missing[@]} -gt 0 ]; then
    say "Missing: ${missing[*]}"
    say "Fedora:  sudo dnf install cmake gcc-c++ pkgconf gtkmm4.0-devel spdlog-devel evolution-data-server-devel"
    say "Ubuntu:  sudo apt install cmake g++ pkg-config libgtkmm-4.0-dev libspdlog-dev libecal2.0-dev"
    exit 1
fi
command -v rsync >/dev/null || say "Note: rsync is not installed -- jot's backups will need it (sudo dnf install rsync)."
say "ok"

# ── 2. a release build, in its own tree ─────────────────────────────────────
# build-release/, not build/: your debug tree is left exactly as it was.
step "Building jot (release) in build-release/"
say "(the first time takes a few minutes; a re-run only rebuilds what changed)"
mkdir -p build-release
# Only the jot target: the selftest is a big file that takes far longer to
# optimise than to run, so it is built in the debug tree (step 3) instead.
if ! { cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" &&
       cmake --build build-release --target jot -- -j"$(nproc)"; } > build-release/build.log 2>&1; then
    grep -E "error|Error" build-release/build.log | head -20
    say "The build failed -- nothing was installed. Full log: build-release/build.log"
    exit 1
fi
say "ok"

step "Running the selftest"
if ! { cmake -S . -B build && cmake --build build --target jot_selftest -- -j"$(nproc)"; } \
        > build-release/selftest-build.log 2>&1; then
    grep -E "error|Error" build-release/selftest-build.log | head -20
    say "The selftest did not build -- nothing was installed. Log: build-release/selftest-build.log"
    exit 1
fi
if ! ./build/jot_selftest > build-release/selftest.log 2>&1; then
    tail -20 build-release/selftest.log
    say "The selftest failed -- nothing was installed. Full log: build-release/selftest.log"
    exit 1
fi
tail -1 build-release/selftest.log

# ── 3. a running jot steps aside ────────────────────────────────────────────
# Asked, not killed: app.quit is the polite door (it saves, and asks first if a
# scratch note would be lost). NameHasOwner FIRST -- ActivateAction on a jot
# that is not running would START one, through the D-Bus service.
jot_running() {
    gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.NameHasOwner "$APP_ID" 2>/dev/null | grep -q true
}
WAS_RUNNING=0
if command -v gdbus >/dev/null && jot_running; then
    WAS_RUNNING=1
    step "jot is running -- asking it to quit so the new copy takes over"
    gdbus call --session --dest "$APP_ID" --object-path "$OBJ_PATH" \
        --method org.freedesktop.Application.ActivateAction quit '[]' '{}' >/dev/null 2>&1 || true
    for _ in $(seq 1 30); do jot_running || break; sleep 0.5; done
    if jot_running; then
        say "jot is still running (it may be asking you something in its window)."
        say "Installing anyway -- the next launch notices the new program and restarts into it."
    else
        say "it quit"
    fi
fi

# ── 4. the program ──────────────────────────────────────────────────────────
step "Installing"
mkdir -p "$BIN_DIR" "$APPS" "$SERVICES" "$ICONS" "$(dirname "$MANIFEST")"
WAS_LINK=""
[ -L "$BIN" ] && WAS_LINK="$(readlink "$BIN")"
# Copy beside it, then rename over it: a rename replaces a LINK rather than
# writing through it into your build tree, and a running jot keeps its old
# file until it exits instead of crashing ("Text file busy").
install -m 755 build-release/jot "$BIN_DIR/.jot.new"
mv -f "$BIN_DIR/.jot.new" "$BIN"
PUT+=("$BIN")
[ -n "$WAS_LINK" ] && say "(replaced a link to $WAS_LINK)"

# ── 5. launcher, D-Bus service, icon ────────────────────────────────────────
sed "s|@JOT_EXEC@|$BIN|g" "resources/$APP_ID.desktop.in" > "$APPS/$APP_ID.desktop"
sed "s|@JOT_EXEC@|$BIN|g" "resources/$APP_ID.service.in" > "$SERVICES/$APP_ID.service"
cp resources/icons/scalable/apps/jot-logo-symbolic.svg "$ICONS/jot-logo-symbolic.svg"
PUT+=("$APPS/$APP_ID.desktop" "$SERVICES/$APP_ID.service" "$ICONS/jot-logo-symbolic.svg")
command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" 2>/dev/null || true
command -v gtk4-update-icon-cache  >/dev/null && gtk4-update-icon-cache -qtf "$DATA/icons/hicolor" 2>/dev/null || true

# ── 6. the Jot tile ─────────────────────────────────────────────────────────
TILE_NOTE=""
if [ "$TILE" = "1" ] && command -v gnome-shell >/dev/null; then
    NEW_TILE=1; [ -d "$EXT_DIR" ] && NEW_TILE=0
    mkdir -p "$EXT_DIR"
    cp -r "extension/$UUID/." "$EXT_DIR/"
    PUT+=("$EXT_DIR/")
    # gsettings, not `gnome-extensions enable`: on Wayland the shell does not
    # know a brand-new extension until the next login (see install-extension.sh).
    if command -v gsettings >/dev/null; then
        cur="$(gsettings get org.gnome.shell enabled-extensions)"
        case "$cur" in
            *"'$UUID'"*) ;;
            "@as []"|"[]") gsettings set org.gnome.shell enabled-extensions "['$UUID']" ;;
            *) gsettings set org.gnome.shell enabled-extensions "${cur%]}, '$UUID']" ;;
        esac
    fi
    if [ "$NEW_TILE" = "1" ]; then TILE_NOTE="Log out and back in once to see the Jot tile in quick settings."
    else TILE_NOTE="The Jot tile was updated -- log out and in to load its new code (only if it changed)."; fi
elif [ "$TILE" = "1" ]; then
    say "(no GNOME Shell here -- skipped the Jot tile)"
fi

# ── 7. the capture hotkey follows the program ───────────────────────────────
# Set in jot's Preferences, it names the binary that set it -- often the build
# tree. Point it at the installed one so it survives a clean of build/.
HOTKEY_NOTE=""
if command -v gsettings >/dev/null; then
    list="$(gsettings get "$KB_SCHEMA" custom-keybindings 2>/dev/null || true)"
    case "$list" in
        *"'$SLOT'"*)
            gsettings set "$KB_SCHEMA.custom-keybinding:$SLOT" command "'$BIN --capture'"
            key="$(gsettings get "$KB_SCHEMA.custom-keybinding:$SLOT" binding 2>/dev/null || echo '?')"
            HOTKEY_NOTE="capture hotkey $key now runs $BIN --capture"
            ;;
    esac
fi

# ── 8. Start at login ───────────────────────────────────────────────────────
if [ "$AUTO" = "ask" ]; then
    if [ "$YES" = "1" ] || [ ! -t 0 ]; then
        [ -f "$AUTOSTART" ] && AUTO="on" || AUTO="off"
    else
        def="n"; [ -f "$AUTOSTART" ] && def="y"
        printf '\nStart jot at login, in the background (no window)? [%s] ' \
            "$([ "$def" = y ] && echo Y/n || echo y/N)"
        read -r ans || ans=""
        ans="${ans:-$def}"
        case "$ans" in [Yy]*) AUTO="on" ;; *) AUTO="off" ;; esac
    fi
fi
if [ "$AUTO" = "on" ]; then
    mkdir -p "$(dirname "$AUTOSTART")"
    cat > "$AUTOSTART" <<EOF
[Desktop Entry]
Type=Application
Name=jot
Comment=Start jot in the background at login -- due notifications and captures from the start
Exec=$BIN --background
Icon=jot-logo-symbolic
Terminal=false
NoDisplay=true
X-GNOME-Autostart-enabled=true
EOF
    PUT+=("$AUTOSTART")
else
    rm -f "$AUTOSTART"
fi

# ── 9. a record, for uninstall.sh and for "where did this come from?" ───────
{
    echo "# jot install record -- written by install.sh, read by uninstall.sh"
    echo "installed: $(date '+%Y-%m-%d %H:%M')"
    echo "from: $SRC"
    git -C "$SRC" rev-parse --short HEAD 2>/dev/null | sed 's/^/commit: /' || true
    for f in "${PUT[@]}"; do echo "file: $f"; done
    [ -n "$HOTKEY_NOTE" ] && echo "hotkey: $SLOT"
    echo "autostart: $AUTO"
} > "$MANIFEST"

# ── what went where ─────────────────────────────────────────────────────────
step "Installed"
for f in "${PUT[@]}"; do say "  $f"; done
[ -n "$HOTKEY_NOTE" ] && say "  $HOTKEY_NOTE"
say ""
if [ "$AUTO" = "on" ]; then
    say "Start at login: on  (jot --background; turn on Preferences > Keep running in the"
    say "  background too, or closing the window ends it until the next login)"
else
    say "Start at login: off  (./install.sh --autostart to turn it on)"
fi
case ":$PATH:" in
    *":$BIN_DIR:"*) say "Run it: jot  (or from Activities)" ;;
    *) say "Run it from Activities. $BIN_DIR is not on your PATH yet -- log out and in,"
       say "  or add it in ~/.bashrc, to type 'jot' in a terminal." ;;
esac
[ -n "$TILE_NOTE" ] && say "$TILE_NOTE"
[ "$WAS_RUNNING" = "1" ] && say "jot was running: start it again from Activities (or: jot)."
say "Your jots folders were not touched. ./uninstall.sh takes all of this out."
