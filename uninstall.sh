#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# uninstall.sh -- take out everything install.sh (or the older
# install-desktop.sh / install-extension.sh) put in.
#
#   ./uninstall.sh                 takes jot out; ASKS about your settings (default keep)
#   ./uninstall.sh --keep-settings no question; settings stay
#   ./uninstall.sh --purge         no question; settings go too
#   ./uninstall.sh -y              no questions at all (settings stay)
#
# NEVER touched: your jots folders, wherever they are, and this source folder.
#
# Your settings live in ~/.local/share/jot: preferences, the Recent jots list,
# captures still waiting to be filed, and (when backups land) your backups.
# Kept unless you say otherwise -- a reinstall picks them straight back up.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

APP_ID="io.github.scott8420.Jot"
OBJ_PATH="/io/github/scott8420/Jot"
UUID="jot-capture@scott8420.github.io"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
CONF="${XDG_CONFIG_HOME:-$HOME/.config}"
BIN="$HOME/.local/bin/jot"
JOT_DATA="$DATA/jot"
SLOT="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/jot-capture/"
KB_SCHEMA="org.gnome.settings-daemon.plugins.media-keys"
CAL_SOURCE="$CONF/evolution/sources/jot-calendar.source"
CAL_DATA="$DATA/evolution/calendar/jot-calendar"

SETTINGS="ask"; YES=0
for a in "$@"; do
    case "$a" in
        --keep-settings) SETTINGS="keep" ;;
        --purge)         SETTINGS="purge" ;;
        -y|--yes)        YES=1 ;;
        -h|--help)       sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "uninstall.sh: unknown option '$a' (try --help)" >&2; exit 2 ;;
    esac
done

say() { printf '%s\n' "$*"; }
GONE=()
gone() { GONE+=("$1"); }
rm_file() { if [ -e "$1" ] || [ -L "$1" ]; then rm -f "$1"; gone "$1"; fi; }

# ── a running jot quits first (politely: it saves) ──────────────────────────
jot_running() {
    gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.NameHasOwner "$APP_ID" 2>/dev/null | grep -q true
}
if command -v gdbus >/dev/null && jot_running; then
    say "jot is running -- asking it to quit (it saves first)."
    gdbus call --session --dest "$APP_ID" --object-path "$OBJ_PATH" \
        --method org.freedesktop.Application.ActivateAction quit '[]' '{}' >/dev/null 2>&1 || true
    for _ in $(seq 1 30); do jot_running || break; sleep 0.5; done
    if jot_running; then
        say "jot did not quit -- it may be asking you something in its window."
        say "Answer it (or quit jot), then run ./uninstall.sh again."
        exit 1
    fi
fi

# ── the program: ours only ──────────────────────────────────────────────────
# A file, or a link into a jot build tree (the README's old `ln -s` advice).
# Anything else called jot in ~/.local/bin is not ours to delete.
if [ -L "$BIN" ]; then
    case "$(readlink "$BIN")" in */jot) rm -f "$BIN"; gone "$BIN (a link)" ;; esac
elif [ -f "$BIN" ]; then
    if grep -qa "$APP_ID" "$BIN" 2>/dev/null; then rm_file "$BIN"
    else say "Left $BIN alone -- it does not look like jot."; fi
fi

# ── launcher, D-Bus service, icon, Start at login ───────────────────────────
rm_file "$DATA/applications/$APP_ID.desktop"
rm_file "$DATA/dbus-1/services/$APP_ID.service"
rm_file "$DATA/icons/hicolor/symbolic/apps/jot-logo-symbolic.svg"
rm_file "$CONF/autostart/$APP_ID.desktop"
command -v update-desktop-database >/dev/null && update-desktop-database "$DATA/applications" 2>/dev/null || true
command -v gtk4-update-icon-cache  >/dev/null && [ -d "$DATA/icons/hicolor" ] && \
    gtk4-update-icon-cache -qtf "$DATA/icons/hicolor" 2>/dev/null || true

# ── the Jot tile ────────────────────────────────────────────────────────────
EXT_DIR="$DATA/gnome-shell/extensions/$UUID"
TILE_GONE=0
if [ -d "$EXT_DIR" ]; then
    command -v gnome-extensions >/dev/null && gnome-extensions disable "$UUID" 2>/dev/null || true
    rm -rf "$EXT_DIR"; gone "$EXT_DIR/"; TILE_GONE=1
fi
if command -v gsettings >/dev/null; then
    cur="$(gsettings get org.gnome.shell enabled-extensions 2>/dev/null || true)"
    case "$cur" in
        *"'$UUID'"*)
            new="$(printf '%s' "$cur" | sed "s#, '$UUID'##; s#'$UUID', ##; s#'$UUID'##")"
            [ "$new" = "[]" ] && new="@as []"
            gsettings set org.gnome.shell enabled-extensions "$new"
            ;;
    esac
fi

# ── the global capture hotkey ───────────────────────────────────────────────
# Left in, it would run a program that is gone every time the key is pressed.
if command -v gsettings >/dev/null; then
    list="$(gsettings get "$KB_SCHEMA" custom-keybindings 2>/dev/null || true)"
    case "$list" in
        *"'$SLOT'"*)
            key="$(gsettings get "$KB_SCHEMA.custom-keybinding:$SLOT" binding 2>/dev/null || echo '?')"
            new="$(printf '%s' "$list" | sed "s#, '$SLOT'##; s#'$SLOT', ##; s#'$SLOT'##")"
            [ "$new" = "[]" ] && new="@as []"
            gsettings set "$KB_SCHEMA" custom-keybindings "$new"
            for k in name command binding; do
                gsettings reset "$KB_SCHEMA.custom-keybinding:$SLOT" "$k" 2>/dev/null || true
            done
            gone "the capture hotkey $key"
            ;;
    esac
fi

# ── the "jot" calendar in GNOME's clock drop-down ───────────────────────────
# A one-way copy of your due todos, made by jot -- nothing in it is only there.
if [ -f "$CAL_SOURCE" ]; then
    rm -f "$CAL_SOURCE"; rm -rf "$CAL_DATA"
    gone "the jot calendar (GNOME's clock drop-down; gone after the next login)"
fi

# ── your settings: asked ────────────────────────────────────────────────────
KEPT=""
if [ -d "$JOT_DATA" ]; then
    rm -f "$JOT_DATA/installed.txt"
    waiting=0
    [ -d "$JOT_DATA/pending" ] && waiting="$(find "$JOT_DATA/pending" -type f | wc -l)"
    backups=0
    [ -d "$JOT_DATA/backups" ] && backups="$(find "$JOT_DATA/backups" -mindepth 2 -maxdepth 2 -type d | wc -l)"
    if [ "$SETTINGS" = "ask" ]; then
        if [ "$YES" = "1" ] || [ ! -t 0 ]; then
            SETTINGS="keep"
        else
            say ""
            say "Your settings are in $JOT_DATA:"
            say "  preferences, the Recent jots list"
            [ "$waiting" -gt 0 ] && say "  $waiting capture(s) still waiting to be filed -- these are notes!"
            [ "$backups" -gt 0 ] && say "  $backups backup(s) of your jots folders"
            printf 'Remove them too? [y/N] '
            read -r ans || ans=""
            case "$ans" in [Yy]*) SETTINGS="purge" ;; *) SETTINGS="keep" ;; esac
        fi
    fi
    if [ "$SETTINGS" = "purge" ]; then
        rm -rf "$JOT_DATA"; gone "$JOT_DATA/ (settings$( [ "$waiting" -gt 0 ] && echo ", $waiting waiting captures")$( [ "$backups" -gt 0 ] && echo ", $backups backups"))"
    else
        KEPT="$JOT_DATA/"
    fi
fi

# ── what went ───────────────────────────────────────────────────────────────
say ""
if [ ${#GONE[@]} -eq 0 ]; then
    say "Nothing of jot's was installed."
else
    say "Removed:"
    for g in "${GONE[@]}"; do say "  $g"; done
fi
[ -n "$KEPT" ] && say "Kept: $KEPT  (settings -- ./uninstall.sh --purge removes them)"
say "Not touched: your jots folders, and this source folder."
[ "$TILE_GONE" = "1" ] && say "The Jot tile leaves quick settings at your next login."
exit 0
