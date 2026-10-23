#!/usr/bin/env bash
set -euo pipefail

GREEN=$'\033[32m'; CYAN=$'\033[36m'; RED=$'\033[31m'; RESET=$'\033[0m'

# ---- Platform detection ----------------------------------------------------
PLATFORM="$(uname -ms)"
case "$PLATFORM" in
    MINGW64*|MSYS_NT*|CYGWIN*)
        echo -e "\n${CYAN}==>${RESET} Native Windows detected."
        echo -e "${RED}error:${RESET} This bash script is for Linux and macOS."
        echo
        echo "To uninstall Pomodoro on Windows, open PowerShell and run:"
        echo "  powershell -c \"irm https://raw.githubusercontent.com/kaustubh-26/pomodoro/main/uninstall.ps1 | iex\""
        echo
        echo "Or, manually:"
        echo "  1. Open Task Manager and end any 'pomodoro.exe' processes."
        echo "  2. Delete the folder '%LOCALAPPDATA%\\Programs\\Pomodoro'."
        echo
        exit 0
        ;;
esac

# ----------------------------------------------------------------------------

PREFIX="${PREFIX:-$HOME/.local}"
LIBDIR="$PREFIX/lib/pomodoro"
BINDIR="$PREFIX/bin"

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
# Only prompt if we have a TTY (prevents hanging in CI/CD)
if [ -t 0 ]; then
    read -r -p "Also remove config and session logs? [y/N] " ans < /dev/tty
else
    ans="n"
fi

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