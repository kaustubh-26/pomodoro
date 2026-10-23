# Pomodoro

[![Latest release](https://img.shields.io/github/v/release/kaustubh-26/pomodoro)](https://github.com/kaustubh-26/pomodoro/releases/latest)

A minimalist, cross-platform Pomodoro timer for the terminal, written in C++20.
Includes a system-tray indicator, a live attached client (`pomo`),
desktop notifications, session logging, and custom per-phase sounds.

> **Cross-Platform:** Works natively on **Linux** (using GTK 3, AppIndicator, and Unix domain sockets) and **Windows** (using native Win32 system tray, WinMM audio, and Named Pipes).

## Features

- **Terminal UI** — full-screen, color-coded, live-updating timer
- **Daemon mode** — run in the background with `--daemon`
- **Tray indicator** — shows the phase and live countdown in the system tray (AppIndicator on Linux, notification area on Windows)
- **Attach client** — `pomo` mirrors the timer like `htop` for your Pomodoro
- **Desktop notifications** — via `notify-send` (Linux) or system notification (Windows)
- **Session logging** — CSV logging to track your daily focus time
- **Custom sounds** — WAV audio for every phase transition (via `paplay`/`pw-play` on Linux, native `PlaySound` on Windows)
- **Native & Portable** — pure C++20 with a lightweight platform abstraction layer (`platform.h`)

## Platform Architecture

| Feature | Linux | Windows |
|---|---|---|
| **System Tray** | GTK 3 + AppIndicator | Native Win32 Tray (`Shell_NotifyIcon`) |
| **Daemon / Background** | Double `fork()` + `setsid()` | Detached process (`SW_HIDE`) |
| **IPC** | Unix Domain Socket (`/tmp/pomodoro.sock`) | Named Pipe (`\\.\pipe\pomodoro`) |
| **Audio** | `paplay` / `pw-play` | Windows Multimedia (`PlaySoundA` / `winmm.lib`) |
| **Raw Terminal** | POSIX `termios` | Windows Console Virtual Terminal Processing |
| **Config Path** | `~/.config/pomodoro/config` | `%APPDATA%\pomodoro\config` |
| **Session Log** | `~/.local/share/pomodoro/sessions.csv` | `%APPDATA%\pomodoro\data\sessions.csv` |

## Requirements

### Linux

- A C++20 compiler (GCC 11+ or Clang 14+)
- GTK 3 development headers (`libgtk-3-dev`)
- libappindicator headers (`libappindicator3-dev` or `libayatana-appindicator3-dev`)
- `notify-send` (from `libnotify-bin` / `libnotify`)
- `paplay` (from `pulseaudio-utils`) or `pw-play` (from `pipewire-bin`)
- On GNOME: the **AppIndicator and KStatusNotifierItem Support** extension

### Windows

- A C++20 compiler:
  - **MSVC** (Visual Studio 2022+ / C++20)
  - or **MinGW-w64** (GCC 11+)
- No external GUI dependencies required (uses native Windows APIs)

## Install & Build

### 🐧 macOS & Linux (One-liner)

To install Pomodoro, run the following command in your terminal:

```bash
curl -fsSL https://raw.githubusercontent.com/kaustubh-26/pomodoro/main/install.sh | bash
```

The script detects your distro, offers to install build dependencies (GTK 3, AppIndicator, etc.), downloads the source, builds it, and installs the binaries to ~/.local/bin.

### 🪟 Windows (One-liner)
To install Pomodoro on Windows, open PowerShell (not Command Prompt) and run:

```powershell
powershell -ExecutionPolicy Bypass -c "irm https://raw.githubusercontent.com/kaustubh-26/pomodoro/main/install.ps1 | iex"
```

(Note: If Windows Defender prompts you, click More info → Run anyway. This is a standard false positive for scripts that download and install software.)

The script downloads the latest pre-compiled release, extracts it to %LOCALAPPDATA%\Programs\Pomodoro, and adds it to your system PATH.

(Alternatively, you can download the pomodoro-windows-x86_64.zip file directly from the Releases page and extract it anywhere).

### Build From source

Prerequisites
- CMake (3.15+)
- Linux: g++, libgtk-3-dev, libappindicator3-dev
- Windows: Visual Studio C++ Build Tools or MinGW-w64


#### Using CMake (Recommended)
```bash
git clone https://github.com/kaustubh-26/pomodoro.git
cd pomodoro
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

#### Manual Linux Build
```bash
make
```

#### Manual Windows Build (MinGW-w64)

```bash
g++ -std=c++20 -O2 -o pomodoro.exe src/pomodoro.cpp -lwinmm
g++ -std=c++20 -O2 -o pomo.exe src/pomo.cpp
g++ -std=c++20 -O2 -mwindows -o pomodoro_tray_indicator.exe src/pomodoro_tray_indicator.cpp -lshell32
```

#### Manual Windows Build (MSVC Developer Command Prompt)

```cmd
cl /std:c++20 /O2 /EHsc src/pomodoro.cpp winmm.lib
cl /std:c++20 /O2 /EHsc src/pomo.cpp
cl /std:c++20 /O2 /EHsc src/pomodoro_tray_indicator.cpp shell32.lib /link /subsystem:windows
```

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

## Configuration

- **Linux:** `~/.config/pomodoro/config`
- **Windows:** `%APPDATA%\pomodoro\config`

```ini
work        = 25
short_break = 5
long_break  = 15
cycles      = 4

notify      = true
bell        = true
log         = true

# Optional: override bundled sounds with absolute paths
# sound_work_to_short = /path/to/work_to_short.wav
# sound_work_to_long  = /path/to/work_to_long.wav
# sound_short_to_work = /path/to/short_to_work.wav
# sound_long_to_work  = /path/to/long_to_work.wav
```

## Sounds

Bundled sounds live alongside the binaries in a `sounds/` directory.
To change them, drop your own WAV files into that directory or set absolute paths in the config file.

## Uninstall

### 🐧 Linux & macOS

```bash
curl -fsSL https://raw.githubusercontent.com/kaustubh-26/pomodoro/main/uninstall.sh | bash
```

#### From a cloned repo:

```bash
./uninstall.sh
```

### 🪟 Windows

Open PowerShell (not Command Prompt) and run:

```powershell
powershell -ExecutionPolicy Bypass -c "irm https://raw.githubusercontent.com/kaustubh-26/pomodoro/main/uninstall.ps1 | iex"
```

#### From a cloned repo:
Open PowerShell, navigate to the repo folder, and bypass the execution policy for this specific script:

```powershell
powershell -ExecutionPolicy Bypass -File .\uninstall.ps1
```

## License

MIT — see [LICENSE](LICENSE).
