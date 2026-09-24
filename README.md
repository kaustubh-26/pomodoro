# Pomodoro

A minimalist Pomodoro timer for the Linux terminal, written in C++20.
Includes a GTK system-tray indicator, a live attached client (`pomo`),
desktop notifications, session logging, and custom per-phase sounds.

> **Linux only.** The tray indicator and daemon features rely on Linux-specific APIs (`/proc/self/exe`, `prctl`). WSL works; native macOS and BSD do not.

## Features

- **Terminal UI** — full-screen, color-coded, live-updating timer
- **Daemon mode** — run in the background with `--daemon`
- **Tray indicator** — shows the phase and live countdown in the top bar
- **Attach client** — `pomo` mirrors the timer like `htop` for your Pomodoro
- **Desktop notifications** via `notify-send`
- **Session logging** to `~/.local/share/pomodoro/sessions.csv`
- **Custom sounds** for every phase transition
- **Portable** — pure C++20 + GTK 3 + AppIndicator

## Requirements

- A C++20 compiler (GCC 11+ or Clang 14+)
- GTK 3 development headers
- libappindicator (or libayatana-appindicator)
- `notify-send` (from `libnotify-bin` / `libnotify`)
- `paplay` (from `pulseaudio-utils`) or `pw-play` (from `pipewire-bin`)
- On GNOME: the **AppIndicator and KStatusNotifierItem Support** extension

## Install

### One-liner (recommended)

```bash
curl -fsSL https://raw.githubusercontent.com/kaustubh-26/pomodoro/main/install.sh | bash
```

The script detects your distro, offers to install build dependencies,
downloads the source, builds it, and installs the binaries to `~/.local/bin`.

### From source

```bash
git clone https://github.com/kaustubh-26/pomodoro.git
cd pomodoro
./install.sh
```

The installer will:

- Detect your distro and offer to install build dependencies
- Build the three binaries
- Install them to `~/.local/lib/pomodoro/`
- Symlink `pomodoro`, `pomo`, and `pomodoro_tray_indicator` into `~/.local/bin/`
- Ensure `~/.local/bin` is on your `$PATH`

## Usage

```bash
# Start in the background with a tray icon
pomodoro --daemon

# Start in the foreground with a UI (q = detach, Q = quit)
pomodoro

# Attach to a running timer from any terminal
pomo
```

### Keys

| Key | Action |
|---|---|
| `space` / `p` | Pause / resume |
| `s` | Skip to next phase |
| `r` | Reset current phase |
| `+` / `-` | Add or remove one minute |
| `q` | Detach (in `pomo` / foreground `pomodoro`) |
| `Q`, `Ctrl-C`, `Esc` | Kill the daemon |

### Options

```
--work=MIN       focus duration        (default 25)
--short=MIN      short break duration  (default 5)
--long=MIN       long break duration   (default 15)
--cycles=N       work sessions before long break (default 4)
--daemon         run as a background daemon
--no-notify      disable desktop notifications
--no-bell        disable terminal bell
--no-log         disable session logging
-h, --help       show help
```

## Configuration

Edit `~/.config/pomodoro/config`:

```ini
work        = 25
short_break = 5
long_break  = 15
cycles      = 4

notify      = true
bell        = true
log         = true

# Optional: override bundled sounds with absolute paths
# sound_work_to_short = /absolute/path/to/file.wav
# sound_work_to_long  = /absolute/path/to/file.wav
# sound_short_to_work = /absolute/path/to/file.wav
# sound_long_to_work  = /absolute/path/to/file.wav
```

## Sounds

Bundled sounds live in `~/.local/lib/pomodoro/sounds/` and are played via
`paplay`. To change them, drop your own WAV files in that folder or set
absolute paths in the config file.

## Uninstall

```bash
curl -fsSL https://raw.githubusercontent.com/kaustubh-26/pomodoro/main/uninstall.sh | bash
```

Or, from a cloned repo:

```bash
./uninstall.sh
```

## License

MIT — see [LICENSE](LICENSE).