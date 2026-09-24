#!/usr/bin/env bash
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"
LIBDIR="$PREFIX/lib/pomodoro"
BINDIR="$PREFIX/bin"

GREEN=$'\033[32m'; CYAN=$'\033[36m'; RESET=$'\033[0m'

echo "${CYAN}==>${RESET} Stopping any running instances"
pkill -f 'pomodoro --daemon' 2>/dev/null || true
pkill -x pomodoro_tray_indicator 2>/dev/null || true
pkill -x pomodoro 2>/dev/null || true
sleep 1

echo "${CYAN}==>${RESET} Removing symlinks"
rm -f "$BINDIR/pomodoro" "$BINDIR/pomo" "$BINDIR/pomodoro_tray_indicator"

echo "${CYAN}==>${RESET} Removing installed files"
rm -rf "$LIBDIR"

echo "${CYAN}==>${RESET} Removing runtime files"
rm -f "${XDG_RUNTIME_DIR:-/tmp}/pomodoro.sock" \
      "${XDG_RUNTIME_DIR:-/tmp}/pomodoro_state.txt"

echo
read -r -p "Also remove config and session logs? [y/N] " ans
case "$ans" in
    [yY]|[yY][eE][sS])
        rm -rf "$HOME/.config/pomodoro" "$HOME/.local/share/pomodoro"
        echo "${GREEN}✓${RESET} Config and logs removed"
        ;;
    *)
        echo "Kept ~/.config/pomodoro and ~/.local/share/pomodoro"
        ;;
esac

echo
echo "${GREEN}✓${RESET} Uninstalled."