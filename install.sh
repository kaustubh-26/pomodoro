#!/usr/bin/env bash
# ============================================================================
#  install.sh — One-line installer for Pomodoro
#  Usage: curl -fsSL https://raw.githubusercontent.com/kaustubh-26/pomodoro/main/install.sh | bash
# ============================================================================
set -euo pipefail

# ---- Configuration ---------------------------------------------------------
REPO_URL="https://github.com/kaustubh-26/pomodoro"
REPO_TARBALL="${REPO_URL}/archive/refs/heads/main.tar.gz"
INSTALL_DIR="$HOME/.local/lib/pomodoro"
BIN_DIR="$HOME/.local/bin"
# ----------------------------------------------------------------------------

# ---- Colors ----------------------------------------------------------------
if [[ -t 1 ]]; then
    BOLD=$'\033[1m'; DIM=$'\033[2m'
    RED=$'\033[31m'; GREEN=$'\033[32m'
    YELLOW=$'\033[33m'; CYAN=$'\033[36m'
    RESET=$'\033[0m'
else
    BOLD=""; DIM=""; RED=""; GREEN=""; YELLOW=""; CYAN=""; RESET=""
fi

error()   { printf '%s✗%s   %s\n' "$RED"   "$RESET" "$*" >&2; }
info()    { printf '%s==>%s %s\n' "$CYAN"  "$RESET" "$*"; }
ok()      { printf '%s✓%s   %s\n' "$GREEN" "$RESET" "$*"; }
warn()    { printf '%s!%s   %s\n' "$YELLOW" "$RESET" "$*"; }
die()     { error "$*"; exit 1; }

# ---- Platform detection ----------------------------------------------------
PLATFORM="$(uname -ms)"
OS=""
ARCH=""

case "$PLATFORM" in
    'Linux x86_64')  OS=linux;  ARCH=x86_64 ;;
    'Linux aarch64'|'Linux arm64') OS=linux; ARCH=aarch64 ;;
    'Darwin x86_64') OS=macos;  ARCH=x86_64 ;;
    'Darwin arm64')  OS=macos;  ARCH=aarch64 ;;
    *) die "Unsupported platform: $PLATFORM" ;;
esac

# Detect WSL
if grep -qi microsoft /proc/version 2>/dev/null; then
    OS=wsl
    warn "Detected WSL — will treat as Linux"
fi

# ---- Dependency checks -----------------------------------------------------
check_cmd() { command -v "$1" >/dev/null 2>&1; }

check_deps() {
    local missing=0

    if ! check_cmd g++; then
        error "g++ not found"
        missing=1
    else
        local gcc_ver
        gcc_ver=$(g++ -dumpversion | cut -d. -f1)
        if [ "$gcc_ver" -lt 11 ]; then
            warn "g++ version $gcc_ver detected; C++20 needs GCC 11+"
        fi
    fi

    check_cmd pkg-config || { error "pkg-config not found"; missing=1; }

    if ! pkg-config --exists gtk+-3.0; then
        error "GTK 3 development files not found"
        missing=1
    fi

    if ! pkg-config --exists appindicator3-0.1 && \
       ! pkg-config --exists ayatana-appindicator3-0.1; then
        error "libappindicator development files not found"
        missing=1
    fi

    return $missing
}

install_deps() {
    local distro=""
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        distro="${ID:-unknown}"
    fi

    info "Installing build dependencies for $distro (requires sudo)"
    case "$distro" in
        ubuntu|debian|linuxmint|pop|kali|raspbian)
            sudo apt update
            sudo apt install -y build-essential pkg-config \
                libgtk-3-dev libappindicator3-dev \
                libnotify-bin pulseaudio-utils
            ;;
        fedora|rhel|centos|rocky|almalinux)
            sudo dnf install -y gcc-c++ make pkgconf-pkg-config \
                gtk3-devel libappindicator-gtk3-devel \
                libnotify pulseaudio-utils
            ;;
        arch|manjaro|endeavouros)
            sudo pacman -S --needed --noconfirm base-devel pkgconf \
                gtk3 libappindicator-gtk3 libnotify libpulse
            ;;
        opensuse*|suse*)
            sudo zypper install -y gcc-c++ make pkg-config \
                gtk3-devel libappindicator3-devel \
                libnotify-tools pulseaudio-utils
            ;;
        *)
            warn "Unknown distro '$distro'. Install manually:"
            echo "    g++, pkg-config, GTK 3 dev, libappindicator dev,"
            echo "    notify-send, paplay"
            return 1
            ;;
    esac
}

# ---- Build & install -------------------------------------------------------
build_and_install() {
    local src_dir="$1"

    # Build
    info "Building binaries..."
    (cd "$src_dir" && \
        g++ -std=c++20 -O2 -Wall -Wextra -o pomodoro src/pomodoro.cpp && \
        g++ -std=c++20 -O2 -Wall -Wextra -o pomo src/pomo.cpp && \
        g++ -std=c++20 -O2 -Wall -Wextra -o pomodoro_tray_indicator \
            src/pomodoro_tray_indicator.cpp \
            $(pkg-config --cflags --libs gtk+-3.0 appindicator3-0.1 2>/dev/null || \
              pkg-config --cflags --libs gtk+-3.0 ayatana-appindicator3-0.1) \
    ) || die "Build failed"

    # Install
    info "Installing to $INSTALL_DIR"
    mkdir -p "$INSTALL_DIR/sounds" "$BIN_DIR"

    cp -f "$src_dir/pomodoro"                "$INSTALL_DIR/"
    cp -f "$src_dir/pomo"                    "$INSTALL_DIR/"
    cp -f "$src_dir/pomodoro_tray_indicator" "$INSTALL_DIR/"

    if [ -d "$src_dir/sounds" ]; then
        cp -f "$src_dir/sounds/"*.wav "$INSTALL_DIR/sounds/" 2>/dev/null || true
    fi

    chmod +x "$INSTALL_DIR/pomodoro" \
             "$INSTALL_DIR/pomo" \
             "$INSTALL_DIR/pomodoro_tray_indicator"

    ln -sf "$INSTALL_DIR/pomodoro"                "$BIN_DIR/pomodoro"
    ln -sf "$INSTALL_DIR/pomo"                    "$BIN_DIR/pomo"
    ln -sf "$INSTALL_DIR/pomodoro_tray_indicator" "$BIN_DIR/pomodoro_tray_indicator"

    ok "Binaries installed"
}

# ---- PATH handling ---------------------------------------------------------
ensure_path() {
    case ":$PATH:" in
        *":$BIN_DIR:"*) return ;;
    esac

    warn "$BIN_DIR is not on your \$PATH"

    local shell_rc=""
    case "${SHELL##*/}" in
        bash) shell_rc="$HOME/.bashrc" ;;
        zsh)  shell_rc="$HOME/.zshrc"  ;;
    esac

    if [ -n "$shell_rc" ] && [ -w "$shell_rc" ]; then
        if ! grep -q "\.local/bin" "$shell_rc" 2>/dev/null; then
            {
                echo ""
                echo "# Added by pomodoro installer"
                echo 'export PATH="$HOME/.local/bin:$PATH"'
            } >> "$shell_rc"
            ok "Added $BIN_DIR to \$PATH in $shell_rc"
            info "Run: source $shell_rc"
        fi
    else
        warn "Add this to your shell config manually:"
        echo '    export PATH="$HOME/.local/bin:$PATH"'
    fi
}

# ---- Main ------------------------------------------------------------------
main() {
    printf '\n%s%sPomodoro Installer%s\n\n' "$BOLD" "$CYAN" "$RESET"

    info "Platform: $OS / $ARCH"

    # Check deps
    if ! check_deps; then
        echo
        read -r -p "Install build dependencies now? [y/N] " ans
        case "$ans" in
            [yY]|[yY][eE][sS])
                install_deps || die "Dependency install failed"
                check_deps || die "Dependencies still missing"
                ;;
            *)
                die "Cannot proceed without build dependencies"
                ;;
        esac
    fi

    # Get source
    local src_dir
    if [ -f "src/pomodoro.cpp" ]; then
        info "Using local source in current directory"
        src_dir="$(pwd)"
    else
        info "Downloading source from GitHub..."
        local tmpdir
        tmpdir="$(mktemp -d)"
        trap 'rm -rf "$tmpdir"' EXIT

        if command -v curl >/dev/null 2>&1; then
            curl -fsSL "$REPO_TARBALL" | tar -xz -C "$tmpdir" --strip-components=1
        elif command -v wget >/dev/null 2>&1; then
            wget -qO- "$REPO_TARBALL" | tar -xz -C "$tmpdir" --strip-components=1
        else
            die "curl or wget required"
        fi
        src_dir="$tmpdir"
    fi

    build_and_install "$src_dir"
    ensure_path

    printf '\n%s%sDone.%s\n\n' "$BOLD" "$GREEN" "$RESET"
    printf 'Quick start:\n'
    printf '  %spomodoro --daemon%s   # background + tray\n' "$BOLD" "$RESET"
    printf '  %spomodoro%s            # foreground UI (q = detach)\n' "$BOLD" "$RESET"
    printf '  %spomo%s                # attach to a running instance\n\n' "$BOLD" "$RESET"
}

main "$@"