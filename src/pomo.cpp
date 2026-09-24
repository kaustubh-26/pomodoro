// ============================================================================
//  pomo.cpp — Attach to a running `pomodoro --daemon` and show live status
// ============================================================================
//
//  Build:
//      g++ -std=c++20 -O2 -Wall -Wextra -o pomo pomo.cpp
//
//  Usage:
//      ./pomo            # attach and view; press q to detach, Q to kill daemon
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <poll.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <thread>
#include <vector>
#include <unistd.h>

namespace ansi {
inline constexpr std::string_view reset        = "\033[0m";
inline constexpr std::string_view bold         = "\033[1m";
inline constexpr std::string_view dim          = "\033[2m";
inline constexpr std::string_view red          = "\033[31m";
inline constexpr std::string_view green        = "\033[32m";
inline constexpr std::string_view yellow       = "\033[33m";
inline constexpr std::string_view cyan         = "\033[36m";
inline constexpr std::string_view grey         = "\033[90m";
inline constexpr std::string_view clear_screen = "\033[H\033[J";
inline constexpr std::string_view hide_cursor  = "\033[?25l";
inline constexpr std::string_view show_cursor  = "\033[?25h";
}

namespace str {
std::string repeat(std::string_view s, std::size_t n) {
    std::string out; out.reserve(s.size() * n);
    for (std::size_t i = 0; i < n; ++i) out.append(s);
    return out;
}
}

// ----------------------------------------------------------------------------
// Paths (must match pomodoro.cpp)
// ----------------------------------------------------------------------------
static std::filesystem::path runtime_dir() {
    if (const char* rt = std::getenv("XDG_RUNTIME_DIR"); rt && *rt) return rt;
    return "/tmp";
}
static std::filesystem::path state_path()  { return runtime_dir() / "pomodoro_state.txt"; }
static std::filesystem::path socket_path() { return runtime_dir() / "pomodoro.sock"; }

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------
struct State {
    std::string phase = "idle";
    int  remaining    = 0;
    bool paused       = false;
    int  cycle        = 0;
    int  run_focus    = 0;
    int  today_secs   = 0;
    bool valid        = false;
};

static State read_state() {
    State s;
    std::ifstream f(state_path());
    if (!f) return s;
    std::string line;
    if (!std::getline(f, line)) return s;

    std::vector<std::string> p;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, '|')) p.push_back(tok);
    if (p.size() < 3) return s;

    s.phase  = p[0];
    try { s.remaining = std::stoi(p[1]); } catch (...) { return s; }
    s.paused = (p[2] == "1");
    if (p.size() > 3) try { s.cycle      = std::stoi(p[3]); } catch (...) {}
    if (p.size() > 4) try { s.run_focus  = std::stoi(p[4]); } catch (...) {}
    if (p.size() > 5) try { s.today_secs = std::stoi(p[5]); } catch (...) {}
    s.valid = true;
    return s;
}

// ----------------------------------------------------------------------------
// Command socket
// ----------------------------------------------------------------------------
static bool send_command(const std::string& cmd) {
    const auto sp = socket_path();
    const int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sp.c_str());

    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(s); return false;
    }
    const std::string line = cmd + "\n";
    const ssize_t n = ::write(s, line.c_str(), line.size());
    (void)n;
    ::close(s);
    return true;
}

static bool daemon_alive() {
    const auto sp = socket_path();
    if (!std::filesystem::exists(sp)) return false;

    const int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sp.c_str());

    const bool ok = (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    ::close(s);
    return ok;
}

// ----------------------------------------------------------------------------
// Terminal guard
// ----------------------------------------------------------------------------
class TerminalGuard {
public:
    TerminalGuard() {
        if (::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &old_) == 0) {
            termios raw = old_;
            raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
            raw.c_cc[VMIN]  = 0;
            raw.c_cc[VTIME] = 0;
            if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) active_ = true;
        }
        std::cout << ansi::hide_cursor << std::flush;
    }
    ~TerminalGuard() {
        if (active_) ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_);
        std::cout << ansi::show_cursor << ansi::reset << std::flush;
    }
    TerminalGuard(const TerminalGuard&) = delete;
    TerminalGuard& operator=(const TerminalGuard&) = delete;
private:
    termios old_{};
    bool    active_ = false;
};

// ----------------------------------------------------------------------------
// Formatting
// ----------------------------------------------------------------------------
static std::string fmt_clock(int secs) {
    if (secs < 0) secs = 0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", secs / 60, secs % 60);
    return buf;
}
static std::string fmt_dur(int secs) {
    if (secs < 0) secs = 0;
    const int h = secs / 3600;
    const int m = (secs % 3600) / 60;
    if (h > 0) return std::to_string(h) + "h " + std::to_string(m) + "m";
    return std::to_string(m) + "m";
}
static std::string_view phase_display(const std::string& p) {
    if (p == "work")        return "FOCUS";
    if (p == "short_break") return "SHORT BREAK";
    if (p == "long_break")  return "LONG BREAK";
    return "IDLE";
}
static std::string_view phase_color(const std::string& p) {
    if (p == "work")        return ansi::red;
    if (p == "short_break") return ansi::green;
    if (p == "long_break")  return ansi::cyan;
    return ansi::reset;
}

// ----------------------------------------------------------------------------
// Render
// ----------------------------------------------------------------------------
static constexpr std::size_t kInner = 46;
static constexpr int         kBarW  = 30;

static std::string render_state(const State& s) {
    std::ostringstream o;
    o << ansi::clear_screen;

    o << "  ┌" << str::repeat("─", kInner + 2) << "┐\n";

    auto push = [&](std::string_view text, std::size_t visible) {
        o << "  │ " << text;
        if (visible < kInner) o << std::string(kInner - visible, ' ');
        o << " │\n";
    };
    auto push_center = [&](std::string_view text, std::size_t visible) {
        const std::size_t pad  = visible < kInner ? (kInner - visible) / 2 : 0;
        const std::size_t used = pad + visible;
        o << "  │ " << std::string(pad, ' ') << text;
        if (used < kInner) o << std::string(kInner - used, ' ');
        o << " │\n";
    };

    o << "  │ " << ansi::bold << ansi::grey << "POMODORO (attached)"
      << ansi::reset << std::string(kInner - 18, ' ') << " │\n";
    push("", 0);

    if (!s.valid) {
        push_center(std::string(ansi::yellow) + "no daemon running" + std::string(ansi::reset), 17);
        push("", 0);
        push_center(std::string(ansi::grey) + "start with: pomodoro --daemon" + std::string(ansi::reset), 28);
        push("", 0);
        o << "  └" << str::repeat("─", kInner + 2) << "┘\n";
        return o.str();
    }

    const auto color = phase_color(s.phase);

    {
        const auto name = phase_display(s.phase);
        const std::string label = std::string(color) + std::string(ansi::bold)
                                + std::string(name) + std::string(ansi::reset);
        push_center(label, name.size());
    }
    {
        const std::string t = fmt_clock(s.remaining);
        push_center(std::string(ansi::bold) + t + std::string(ansi::reset), t.size());
    }
    {
        // We don't know the total anymore (state file doesn't carry it),
        // so draw the bar as a simple remaining-time visual: 30 chars = 25 min
        const int total_est = 25 * 60;
        double frac = 1.0 - static_cast<double>(s.remaining) / total_est;
        frac = std::clamp(frac, 0.0, 1.0);
        const int filled = static_cast<int>(frac * kBarW + 0.5);
        const std::string bar = std::string(color)
                              + str::repeat("█", static_cast<std::size_t>(filled))
                              + std::string(ansi::dim)
                              + str::repeat("░", static_cast<std::size_t>(kBarW - filled))
                              + std::string(ansi::reset);
        push_center(bar, kBarW);
    }
    push("", 0);

    {
        const int done = s.cycle % 4;
        std::string dots;
        for (int i = 0; i < 4; ++i) {
            if (!dots.empty()) dots += ' ';
            if (i < done) dots += std::string(color) + "●" + std::string(ansi::reset);
            else          dots += std::string(ansi::grey) + "○" + std::string(ansi::reset);
        }
        push_center(dots, 7);
    }
    {
        std::ostringstream st;
        st << "Session " << s.run_focus << "   ·   Today " << fmt_dur(s.today_secs);
        const std::string text = st.str();
        push_center(std::string(ansi::grey) + text + std::string(ansi::reset), text.size());
    }
    {
        std::string text; std::string_view col;
        if (s.paused) { text = "[ PAUSED ]";  col = ansi::yellow; }
        else          { text = "[ RUNNING ]"; col = ansi::green;  }
        push_center(std::string(col) + text + std::string(ansi::reset), text.size());
    }
    push("", 0);

    o << "  └" << str::repeat("─", kInner + 2) << "┘\n";
    o << "\n  " << ansi::dim
      << "space/p pause   s skip   r reset   +/- 1m   q detach   Q kill daemon"
      << ansi::reset << "\n";
    return o.str();
}

// ----------------------------------------------------------------------------
// Key handling
// ----------------------------------------------------------------------------
static bool handle_key(char c, bool& kill_daemon) {
    switch (c) {
        case ' ': case 'p': case 'P': send_command("toggle");   return true;
        case 's': case 'S':           send_command("skip");     return true;
        case 'r': case 'R':           send_command("reset");    return true;
        case '+': case '=':           send_command("adjust+60");return true;
        case '-': case '_':           send_command("adjust-60");return true;
        case 'q':                     return false;               // detach only
        case 'Q': kill_daemon = true; send_command("quit");     return false;
        case 3:  kill_daemon = true;  send_command("quit");     return false;
        case 27: kill_daemon = true;  send_command("quit");     return false;
        default: return true;
    }
}

// ----------------------------------------------------------------------------
// main
// ----------------------------------------------------------------------------
int main() {
    std::signal(SIGPIPE, SIG_IGN);
    if (!std::filesystem::exists(socket_path())) {
        std::cerr << "No daemon running.\n"
                  << "Start it with:  pomodoro --daemon\n";
        return 1;
    }

    TerminalGuard guard;

    std::optional<State> last_shown;
    bool kill_daemon = false;
    bool running     = true;

    while (running) {
        // ---- read state
        State s = read_state();

        if (!daemon_alive()) {
            s.valid = false;    // forces "no daemon running" screen
        }

        // ---- render only when changed
        if (!last_shown ||
            last_shown->phase      != s.phase ||
            last_shown->remaining  != s.remaining ||
            last_shown->paused     != s.paused ||
            last_shown->cycle      != s.cycle ||
            last_shown->run_focus  != s.run_focus ||
            last_shown->today_secs != s.today_secs)
        {
            std::cout << render_state(s) << std::flush;
            last_shown = s;
        }

        // ---- input
        pollfd pfd{STDIN_FILENO, POLLIN, 0};
        const int r = ::poll(&pfd, 1, 50);
        if (r > 0 && (pfd.revents & POLLIN)) {
            char c = 0;
            if (::read(STDIN_FILENO, &c, 1) == 1) {
                if (!handle_key(c, kill_daemon)) running = false;
            }
        }
    }

    std::cout << ansi::clear_screen;
    if (kill_daemon)
        std::cout << "Daemon stopped.\n";
    else
        std::cout << "Detached (daemon still running).\n";
    return 0;
}