// ============================================================================
//  pomodoro.cpp — A Pomodoro timer for the Linux terminal (C++20)
// ============================================================================
//
//  Build:
//      g++ -std=c++20 -O2 -Wall -Wextra -o pomodoro pomodoro.cpp
//
//  Run:
//      ./pomodoro
//      ./pomodoro --work=50 --short=10 --long=30 --cycles=3
//      ./pomodoro --no-notify --no-bell --help
//
// ============================================================================

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <poll.h>
#include <sstream>
#include <string>
#include <string_view>
#include <termios.h>
#include <thread>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <sys/wait.h>

// ============================================================================
//  ANSI helpers
// ============================================================================
namespace ansi
{
    inline constexpr std::string_view reset = "\033[0m";
    inline constexpr std::string_view bold = "\033[1m";
    inline constexpr std::string_view dim = "\033[2m";
    inline constexpr std::string_view red = "\033[31m";
    inline constexpr std::string_view green = "\033[32m";
    inline constexpr std::string_view yellow = "\033[33m";
    inline constexpr std::string_view blue = "\033[34m";
    inline constexpr std::string_view cyan = "\033[36m";
    inline constexpr std::string_view grey = "\033[90m";
    inline constexpr std::string_view clear_screen = "\033[H\033[J";
    inline constexpr std::string_view hide_cursor = "\033[?25l";
    inline constexpr std::string_view show_cursor = "\033[?25h";
} // namespace ansi

// ============================================================================
//  Signal handling
// ============================================================================
static volatile std::sig_atomic_t g_stop = 0;

static void on_signal(int) { g_stop = 1; }

// ============================================================================
//  Small string utilities
// ============================================================================
namespace str
{

    std::string trim(std::string_view s)
    {
        std::size_t b = 0, e = s.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
            ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
            --e;
        return std::string(s.substr(b, e - b));
    }

    std::string repeat(std::string_view s, std::size_t n)
    {
        std::string out;
        out.reserve(s.size() * n);
        for (std::size_t i = 0; i < n; ++i)
            out.append(s);
        return out;
    }

    std::vector<std::string> split(std::string_view s, char delim)
    {
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (true)
        {
            const std::size_t pos = s.find(delim, start);
            if (pos == std::string_view::npos)
            {
                parts.emplace_back(s.substr(start));
                break;
            }
            parts.emplace_back(s.substr(start, pos - start));
            start = pos + 1;
        }
        return parts;
    }

    std::string shell_quote(std::string_view s)
    {
        std::string out = "'";
        for (char c : s)
        {
            if (c == '\'')
                out += "'\\''";
            else
                out += c;
        }
        out += '\'';
        return out;
    }

} // namespace str

// ============================================================================
//  Time helpers
// ============================================================================
namespace tmutil
{

    std::string today()
    {
        const std::time_t t = std::time(nullptr);
        std::tm tmv{};
        localtime_r(&t, &tmv);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
        return buf;
    }

    std::string timestamp()
    {
        const std::time_t t = std::time(nullptr);
        std::tm tmv{};
        localtime_r(&t, &tmv);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
        return buf;
    }

    std::string format_clock(std::chrono::milliseconds ms)
    {
        if (ms.count() < 0)
            ms = std::chrono::milliseconds(0);
        const auto total = std::chrono::duration_cast<std::chrono::seconds>(ms).count();
        std::ostringstream o;
        o << std::setfill('0') << std::setw(2) << (total / 60)
          << ':' << std::setw(2) << (total % 60);
        return o.str();
    }

    std::string format_duration(std::chrono::seconds s)
    {
        const long long total = std::max<long long>(0, s.count());
        const long long h = total / 3600;
        const long long m = (total % 3600) / 60;
        if (h > 0)
            return std::to_string(h) + "h " + std::to_string(m) + "m";
        return std::to_string(m) + "m";
    }

} // namespace tmutil

// ============================================================================
//  Paths
// ============================================================================
namespace paths
{

    std::filesystem::path config_dir()
    {
        if (const char *x = std::getenv("XDG_CONFIG_HOME"); x && *x)
            return std::filesystem::path(x) / "pomodoro";
        const char *home = std::getenv("HOME");
        return std::filesystem::path(home ? home : ".") / ".config" / "pomodoro";
    }

    std::filesystem::path data_dir()
    {
        if (const char *x = std::getenv("XDG_DATA_HOME"); x && *x)
            return std::filesystem::path(x) / "pomodoro";
        const char *home = std::getenv("HOME");
        return std::filesystem::path(home ? home : ".") / ".local" / "share" / "pomodoro";
    }

    std::filesystem::path config_file() { return config_dir() / "config"; }
    std::filesystem::path log_file() { return data_dir() / "sessions.csv"; }

    inline std::filesystem::path runtime_dir()
    {
        if (const char *rt = std::getenv("XDG_RUNTIME_DIR"); rt && *rt)
            return rt;
        return "/tmp";
    }

    inline std::filesystem::path state_file() { return runtime_dir() / "pomodoro_state.txt"; }
    inline std::filesystem::path socket_file() { return runtime_dir() / "pomodoro.sock"; }

} // namespace paths

// ============================================================================
//  Executable-relative paths (so sounds ship alongside the binary)
// ============================================================================
static std::filesystem::path exe_dir() {
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec || p.empty())
        return std::filesystem::current_path();
    return p.parent_path();
}

static std::filesystem::path bundled_sound(const std::string& name) {
    return exe_dir() / "sounds" / name;
}

// ============================================================================
//  Is a pomodoro daemon already running?
// ============================================================================
static bool daemon_is_running() {
    const auto sp = paths::socket_file();
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

// ============================================================================
//  Configuration
// ============================================================================
struct Config
{
    int work_min = 25;
    int short_break_min = 5;
    int long_break_min = 15;
    int cycles = 4; // work sessions before a long break
    bool notify = true;
    bool bell = true;
    bool log = true;
    std::string notify_cmd = "notify-send";
    std::string sound_work_to_short = bundled_sound("work_to_short.wav").string();
    std::string sound_work_to_long  = bundled_sound("work_to_long.wav").string();
    std::string sound_short_to_work = bundled_sound("short_to_work.wav").string();
    std::string sound_long_to_work  = bundled_sound("long_to_work.wav").string();

    void sanitize()
    {
        work_min = std::clamp(work_min, 1, 24 * 60);
        short_break_min = std::clamp(short_break_min, 1, 24 * 60);
        long_break_min = std::clamp(long_break_min, 1, 24 * 60);
        cycles = std::clamp(cycles, 1, 100);
    }

    static bool parse_bool(const std::string &v)
    {
        return v == "true" || v == "1" || v == "yes" || v == "on";
    }

    static Config load(const std::filesystem::path &path)
    {
        Config c;
        std::ifstream f(path);
        if (!f)
            return c;

        std::string line;
        while (std::getline(f, line))
        {
            if (const auto hash = line.find('#'); hash != std::string::npos)
                line = line.substr(0, hash);

            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            const std::string key = str::trim(line.substr(0, eq));
            const std::string val = str::trim(line.substr(eq + 1));
            if (key.empty() || val.empty())
                continue;

            try
            {
                if (key == "work")
                    c.work_min = std::stoi(val);
                else if (key == "short_break")
                    c.short_break_min = std::stoi(val);
                else if (key == "long_break")
                    c.long_break_min = std::stoi(val);
                else if (key == "cycles")
                    c.cycles = std::stoi(val);
                else if (key == "notify")
                    c.notify = parse_bool(val);
                else if (key == "bell")
                    c.bell = parse_bool(val);
                else if (key == "log")
                    c.log = parse_bool(val);
                else if (key == "notify_cmd")
                    c.notify_cmd = val;
                else if (key == "sound_work_to_short") 
                    c.sound_work_to_short = val;
                else if (key == "sound_work_to_long")  
                    c.sound_work_to_long  = val;
                else if (key == "sound_short_to_work") 
                    c.sound_short_to_work = val;
                else if (key == "sound_long_to_work")  
                    c.sound_long_to_work  = val;
            }
            catch (...)
            { /* ignore malformed values */
            }
        }
        c.sanitize();
        return c;
    }

    static void write_default(const std::filesystem::path &path)
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream f(path);
        if (!f)
            return;
        f << "# Pomodoro timer configuration\n"
             "# All durations are in minutes.\n\n"
             "work        = 25\n"
             "short_break = 5\n"
             "long_break  = 15\n"
             "cycles      = 4      # work sessions before a long break\n\n"
             "notify      = true   # desktop notification via notify-send\n"
             "bell        = true   # terminal bell\n"
             "log         = true   # append sessions to the CSV log\n";
    }
};

// ============================================================================
//  Terminal guard: raw mode + hidden cursor
// ============================================================================
class TerminalGuard
{
public:
    TerminalGuard()
    {
        if (::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &old_) == 0)
        {
            termios raw = old_;
            raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0)
                active_ = true;
        }
        std::cout << ansi::hide_cursor << std::flush;
    }

    ~TerminalGuard()
    {
        if (active_)
            ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_);
        std::cout << ansi::show_cursor << ansi::reset << std::flush;
    }

    TerminalGuard(const TerminalGuard &) = delete;
    TerminalGuard &operator=(const TerminalGuard &) = delete;

private:
    termios old_{};
    bool active_ = false;
};

// ============================================================================
//  Daemonize: detach from terminal, become a background process
// ============================================================================
static bool daemonize()
{
    pid_t pid = ::fork();
    if (pid < 0)
        return false;
    if (pid > 0)
        std::exit(0); // parent exits
    if (::setsid() < 0)
        return false;

    std::signal(SIGHUP, SIG_IGN);

    pid = ::fork();
    if (pid < 0)
        return false;
    if (pid > 0)
        std::exit(0); // session leader exits

    ::umask(0);
    if (::chdir("/") < 0)
        return false;

    const int fd = ::open("/dev/null", O_RDWR);
    if (fd < 0)
        return false;
    ::dup2(fd, STDIN_FILENO);
    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);
    if (fd > 2)
        ::close(fd);
    return true;
}

// ============================================================================
//  Fork a foreground process into a real background daemon.
//  Parent prints a farewell and exits; only the grandchild continues.
// ============================================================================
static bool fork_to_background()
{
    std::cout << std::flush;   // flush before forking so no output is duplicated

    pid_t pid = ::fork();
    if (pid < 0) return false;

    if (pid > 0) {
        // Parent: hand control back to the shell.
        std::cout << "\n  " << ansi::bold << "Detached." << ansi::reset
                  << " Pomodoro running in the background.\n"
                  << "  Re-attach with: " << ansi::bold << "pomo" << ansi::reset
                  << "\n\n" << std::flush;
        std::exit(0);
    }

    // Child: new session, ignore SIGHUP, fork again to fully detach.
    if (::setsid() < 0) return false;
    std::signal(SIGHUP, SIG_IGN);

    pid = ::fork();
    if (pid < 0) return false;
    if (pid > 0) std::exit(0);

    ::umask(0);

    // Redirect standard streams to /dev/null.
    const int fd = ::open("/dev/null", O_RDWR);
    if (fd < 0) return false;
    ::dup2(fd, STDIN_FILENO);
    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);
    if (fd > 2) ::close(fd);

    return true;   // only the daemon grandchild reaches here
}

// ============================================================================
//  Tray indicator lifecycle: spawn as child, die when daemon dies
// ============================================================================
static pid_t g_tray_pid = -1;

static void launch_tray_indicator() {
    const auto tray_path = exe_dir() / "pomodoro_tray_indicator";
    if (!std::filesystem::exists(tray_path)) return;

    const pid_t pid = ::fork();
    if (pid < 0) return;

    if (pid == 0) {
        // We're the child. Ask the kernel to send SIGTERM when parent dies.
        const pid_t parent = ::getppid();
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (::getppid() != parent) ::_exit(0);  // parent already gone

        // Redirect output to /dev/null so we don't pollute the daemon's stdout.
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            if (devnull > 2) ::close(devnull);
        }

        ::execl(tray_path.c_str(), tray_path.c_str(), nullptr);
        ::_exit(127);  // exec failed
    }

    g_tray_pid = pid;
}

static void stop_tray_indicator() {
    if (g_tray_pid <= 0) return;
    ::kill(g_tray_pid, SIGTERM);
    ::waitpid(g_tray_pid, nullptr, 0);
    g_tray_pid = -1;
}

// ============================================================================
//  The application
// ============================================================================
class App
{
public:
    enum class Phase
    {
        Work,
        ShortBreak,
        LongBreak
    };

    explicit App(Config cfg) : cfg_(std::move(cfg))
    {
        cfg_.sanitize();
        log_path_ = paths::log_file();
    }

    void set_daemon(bool d) { daemon_ = d; }

    int run() {
        std::signal(SIGINT,  on_signal);
        std::signal(SIGTERM, on_signal);
        std::signal(SIGPIPE, SIG_IGN);

        today_focus_ = load_today_focus();
        set_phase(Phase::Work);

        // ---------- Mode 1: --daemon ----------
        if (daemon_) {
            if (!setup_daemon_socket()) return 1;
            launch_tray_indicator();
            const int rc = run_daemon_loop();
            stop_tray_indicator();
            return rc;
        }

        // ---------- Mode 2: foreground UI + tray ----------
        if (!setup_daemon_socket()) {
            std::cerr << "Failed to set up socket.\n";
            return 1;
        }
        launch_tray_indicator();

        run_interactive_loop();

        // Did the user press `q` to detach?
        if (exit_mode_ == ExitMode::Detach) {
            // Clean up the current tray and socket before forking.
            stop_tray_indicator();
            if (listen_sock_ >= 0) { ::close(listen_sock_); listen_sock_ = -1; }
            ::unlink(paths::socket_file().c_str());

            if (!fork_to_background()) {
                std::cerr << "Failed to detach.\n";
                return 1;
            }

            // ---- We are now the daemon grandchild. ----
            daemon_     = true;
            running_    = true;
            exit_mode_  = ExitMode::None;

            if (!setup_daemon_socket()) return 1;
            launch_tray_indicator();
            const int rc = run_daemon_loop();
            stop_tray_indicator();
            return rc;
        }

        // ---------- Mode 3: `Q` / Ctrl-C / Esc — clean exit ----------
        stop_tray_indicator();
        return 0;
    }

private:
    using Clock = std::chrono::steady_clock;

    // ---- state -------------------------------------------------------------
    Config cfg_;
    std::filesystem::path log_path_;

    Phase phase_ = Phase::Work;
    std::chrono::milliseconds phase_total_{0};
    std::chrono::milliseconds remaining_{0};

    bool paused_ = false;
    bool running_ = true;
    bool daemon_ = false;
    int listen_sock_ = -1;
    enum class ExitMode { None, Detach, Kill };
    ExitMode exit_mode_ = ExitMode::None;

    int cycle_count_ = 0; // completed work sessions (for long-break cadence)
    int run_focus_ = 0;   // completed work sessions this run

    std::chrono::seconds today_focus_{0};

    struct RenderKey
    {
        Phase phase;
        long long seconds;
        bool paused;
        int cycle_count;
        std::chrono::seconds today;
        bool operator==(const RenderKey &) const = default;
    };
    std::optional<RenderKey> last_key_;

    // ---- phase helpers -----------------------------------------------------
    static std::string_view phase_name(Phase p)
    {
        switch (p)
        {
        case Phase::Work:
            return "FOCUS";
        case Phase::ShortBreak:
            return "SHORT BREAK";
        case Phase::LongBreak:
            return "LONG BREAK";
        }
        return "?";
    }

    static std::string_view phase_key(Phase p)
    {
        switch (p)
        {
        case Phase::Work:
            return "work";
        case Phase::ShortBreak:
            return "short_break";
        case Phase::LongBreak:
            return "long_break";
        }
        return "?";
    }

    static std::string_view phase_color(Phase p)
    {
        switch (p)
        {
        case Phase::Work:
            return ansi::red;
        case Phase::ShortBreak:
            return ansi::green;
        case Phase::LongBreak:
            return ansi::cyan;
        }
        return ansi::reset;
    }

    std::chrono::milliseconds duration_for(Phase p) const
    {
        switch (p)
        {
        case Phase::Work:
            return std::chrono::minutes(cfg_.work_min);
        case Phase::ShortBreak:
            return std::chrono::minutes(cfg_.short_break_min);
        case Phase::LongBreak:
            return std::chrono::minutes(cfg_.long_break_min);
        }
        return std::chrono::minutes(1);
    }

    void set_phase(Phase p)
    {
        phase_ = p;
        phase_total_ = duration_for(p);
        remaining_ = phase_total_;
        paused_ = false;
        last_key_.reset(); // force a redraw
    }

    // ---- phase transitions -------------------------------------------------
    void finish_phase(bool completed)
    {
        const auto elapsed = completed ? phase_total_ : (phase_total_ - remaining_);
        log_phase(phase_, elapsed, completed);

        if (phase_ == Phase::Work && completed)
        {
            ++cycle_count_;
            ++run_focus_;
            today_focus_ += std::chrono::duration_cast<std::chrono::seconds>(elapsed);
        }

        Phase next = Phase::Work;
        std::string title, body;

        if (phase_ == Phase::Work)
        {
            const bool long_break = completed && (cycle_count_ % cfg_.cycles == 0);
            next = long_break ? Phase::LongBreak : Phase::ShortBreak;
            if (completed)
            {
                title = "Focus complete";
                body = long_break ? "Great work — take a long break."
                                  : "Time for a short break.";
            }
        }
        else
        {
            next = Phase::Work;
            if (completed)
            {
                title = "Break over";
                body = "Back to focus.";
            }
        }

        if (completed)
        {
            notify(title, body);

            std::string sound;
            if (phase_ == Phase::Work) {
                const bool long_break = (cycle_count_ % cfg_.cycles == 0);
                sound = long_break ? cfg_.sound_work_to_long : cfg_.sound_work_to_short;
            } else if (phase_ == Phase::ShortBreak) {
                sound = cfg_.sound_short_to_work;
            } else if (phase_ == Phase::LongBreak) {
                sound = cfg_.sound_long_to_work;
            }
            play_sound(sound);

            if (cfg_.bell)
                std::cout << '\a' << std::flush;
        }

        set_phase(next);
    }

    void skip_phase() { finish_phase(/*completed=*/false); }

    void reset_phase()
    {
        remaining_ = phase_total_;
        paused_ = false;
        last_key_.reset();
    }

    void adjust(std::chrono::seconds delta)
    {
        remaining_ = std::clamp(remaining_ + delta,
                                std::chrono::milliseconds(1000),
                                phase_total_);
        last_key_.reset();
    }

    // ---- input -------------------------------------------------------------
    void poll_input()
    {
        for (;;)
        {
            pollfd pfd{STDIN_FILENO, POLLIN, 0};
            const int r = ::poll(&pfd, 1, 0);
            if (r <= 0 || !(pfd.revents & POLLIN))
                return;

            char c = 0;
            const ssize_t n = ::read(STDIN_FILENO, &c, 1);
            if (n <= 0)
                return;

            handle_key(c);
            if (!running_)
                return;
        }
    }

    void handle_key(char c)
    {
        switch (c)
        {
        case ' ':
        case 'p':
        case 'P':
            paused_ = !paused_;
            last_key_.reset();
            break;
        case 's':
        case 'S':
            skip_phase();
            break;
        case 'r':
        case 'R':
            reset_phase();
            break;
        case '+':
        case '=':
            adjust(std::chrono::seconds(60));
            break;
        case '-':
        case '_':
            adjust(std::chrono::seconds(-60));
            break;
        case 'q':
            exit_mode_ = ExitMode::Detach;
            running_   = false;
            break;
        case 'Q':
        case 3:  // Ctrl-C
        case 27: // ESC
            exit_mode_ = ExitMode::Kill;
            running_   = false;
            break;
        default:
            break;
        }
    }

    // ---- notifications & logging ------------------------------------------
    void notify(std::string_view summary, std::string_view body) const
    {
        if (!cfg_.notify)
            return;
        const std::string cmd = cfg_.notify_cmd + " -a Pomodoro -t 6000 " + str::shell_quote(summary) + " " + str::shell_quote(body) + " >/dev/null 2>&1 &";
        if (std::system(cmd.c_str()) != 0)
        { /* ignore */
        }
    }

    void play_sound(const std::string& path) const {
        if (path.empty()) return;
        const std::string cmd = "paplay " + str::shell_quote(path) + " >/dev/null 2>&1 &";
        if (std::system(cmd.c_str()) != 0) { /* ignore */ }
    }

    void log_phase(Phase p, std::chrono::milliseconds elapsed, bool completed) const
    {
        if (!cfg_.log)
            return;

        std::error_code ec;
        std::filesystem::create_directories(log_path_.parent_path(), ec);

        std::ofstream f(log_path_, std::ios::app);
        if (!f)
            return;

        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        f << tmutil::timestamp() << ','
          << phase_key(p) << ','
          << secs << ','
          << (completed ? 1 : 0) << '\n';
    }

    std::chrono::seconds load_today_focus() const
    {
        std::ifstream f(log_path_);
        if (!f)
            return std::chrono::seconds{0};

        const std::string today = tmutil::today();
        long long total = 0;
        std::string line;

        while (std::getline(f, line))
        {
            if (line.size() < today.size())
                continue;
            if (line.compare(0, today.size(), today) != 0)
                continue;

            const auto parts = str::split(line, ',');
            if (parts.size() < 4)
                continue;
            if (parts[1] != "work" || parts[3] != "1")
                continue;

            try
            {
                total += std::stoll(parts[2]);
            }
            catch (...)
            {
            }
        }
        return std::chrono::seconds(total);
    }

    // ---- state file for tray_indicator ------------------------------------
    void write_state_file() const {
        std::ofstream f(paths::state_file(), std::ios::trunc);
        if (!f) return;

        const long long rem_secs =
            std::chrono::duration_cast<std::chrono::seconds>(remaining_).count();

        f << phase_key(phase_) << '|'
          << rem_secs          << '|'
          << (paused_ ? 1 : 0) << '|'
          << cycle_count_      << '|'
          << run_focus_        << '|'
          << today_focus_.count()
          << '\n';
    }

        // ---- interactive (foreground) mode ------------------------------------
    int run_interactive_loop() {
        TerminalGuard guard;
        auto last = Clock::now();

        while (running_ && !g_stop) {
            const auto now = Clock::now();
            const auto dt  = std::chrono::duration_cast<std::chrono::milliseconds>(now - last);
            last = now;

            if (!paused_ && dt.count() > 0) {
                remaining_ -= dt;
                if (remaining_.count() <= 0) {
                    remaining_ = std::chrono::milliseconds(0);
                    finish_phase(true);
                }
            }

            poll_input();
            render();
            write_state_file();
            handle_socket_commands();
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }

        std::cout << "\n  " << ansi::bold << "Session finished." << ansi::reset << "\n"
                  << "  Work sessions completed : " << run_focus_ << "\n"
                  << "  Total focus today       : " << tmutil::format_duration(today_focus_) << "\n\n";
        return 0;
    }

    // ---- daemon (background) mode -----------------------------------------
    bool setup_daemon_socket() {
        const auto sp = paths::socket_file();
        ::unlink(sp.c_str());

        listen_sock_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_sock_ < 0) return false;

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sp.c_str());

        if (::bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(listen_sock_); listen_sock_ = -1; return false;
        }
        if (::listen(listen_sock_, 4) < 0) {
            ::close(listen_sock_); listen_sock_ = -1; return false;
        }

        // Set listening socket non-blocking ONCE so accept() never hangs.
        const int flags = ::fcntl(listen_sock_, F_GETFL, 0);
        ::fcntl(listen_sock_, F_SETFL, flags | O_NONBLOCK);
        return true;
    }
    
    int run_daemon_loop() {
        auto last = Clock::now();

        while (running_ && !g_stop) {
            const auto now = Clock::now();
            const auto dt  = std::chrono::duration_cast<std::chrono::milliseconds>(now - last);
            last = now;

            if (!paused_ && dt.count() > 0) {
                remaining_ -= dt;
                if (remaining_.count() <= 0) {
                    remaining_ = std::chrono::milliseconds(0);
                    finish_phase(true);
                }
            }

            write_state_file();
            handle_socket_commands();

            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }

        if (listen_sock_ >= 0) ::close(listen_sock_);
        ::unlink(paths::socket_file().c_str());
        return 0;
    }

    void handle_socket_commands() {
        if (listen_sock_ < 0) return;

        // 1. Only try to accept if a client is actually waiting.
        pollfd lp{listen_sock_, POLLIN, 0};
        if (::poll(&lp, 1, 0) <= 0) return;
        if (!(lp.revents & POLLIN)) return;

        const int client = ::accept(listen_sock_, nullptr, nullptr);
        if (client < 0) return;

        // 2. Wait up to 100 ms for the client to send its command.
        //    This avoids blocking forever if the client connects but is slow to write.
        pollfd cp{client, POLLIN, 0};
        if (::poll(&cp, 1, 100) <= 0 || !(cp.revents & POLLIN)) {
            ::close(client);
            return;
        }

        char buf[64] = {};
        const ssize_t n = ::read(client, buf, sizeof(buf) - 1);
        if (n <= 0) { ::close(client); return; }

        std::string cmd(buf, static_cast<std::size_t>(n));
        while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
            cmd.pop_back();

        if      (cmd == "pause")        paused_ = true;
        else if (cmd == "resume")       paused_ = false;
        else if (cmd == "toggle")       paused_ = !paused_;
        else if (cmd == "skip")         skip_phase();
        else if (cmd == "reset")        reset_phase();
        else if (cmd == "adjust+60")    adjust(std::chrono::seconds(60));
        else if (cmd == "adjust-60")    adjust(std::chrono::seconds(-60));
        else if (cmd == "quit")         running_ = false;
        else if (cmd == "status")       { /* fall through to reply */ }

        // No reply — the client reads state from the state file.
        ::close(client);
    }

    // ---- rendering ---------------------------------------------------------
    static constexpr std::size_t kInner = 46;
    static constexpr int kBarW = 30;

    void render()
    {
        const long long secs = std::chrono::duration_cast<std::chrono::seconds>(remaining_).count();
        const RenderKey key{phase_, secs, paused_, cycle_count_, today_focus_};
        if (last_key_ && *last_key_ == key)
            return;
        last_key_ = key;

        const std::string_view color = phase_color(phase_);

        std::ostringstream o;
        o << ansi::clear_screen;

        // --- top border
        o << "  ┌" << str::repeat("─", kInner + 2) << "┐\n";

        auto push = [&](std::string_view text, std::size_t visible)
        {
            o << "  │ " << text;
            if (visible < kInner)
                o << std::string(kInner - visible, ' ');
            o << " │\n";
        };
        auto push_center = [&](std::string_view text, std::size_t visible)
        {
            const std::size_t pad = visible < kInner ? (kInner - visible) / 2 : 0;
            const std::size_t used = pad + visible;
            o << "  │ " << std::string(pad, ' ') << text;
            if (used < kInner)
                o << std::string(kInner - used, ' ');
            o << " │\n";
        };

        // --- title
        o << "  │ " << ansi::bold << ansi::grey
          << "POMODORO" << ansi::reset
          << std::string(kInner - 5, ' ') << " │\n";
        push("", 0);

        // --- phase label
        {
            const std::string_view name = phase_name(phase_);
            const std::string label = std::string(color) + std::string(ansi::bold) + std::string(name) + std::string(ansi::reset);
            push_center(label, name.size());
        }

        // --- clock
        {
            const std::string t = tmutil::format_clock(remaining_);
            const std::string line = std::string(ansi::bold) + t + std::string(ansi::reset);
            push_center(line, t.size());
        }

        // --- progress bar
        {
            const double total = static_cast<double>(phase_total_.count());
            const double frac = total > 0.0
                                    ? 1.0 - static_cast<double>(remaining_.count()) / total
                                    : 0.0;
            const int filled = static_cast<int>(std::clamp(frac, 0.0, 1.0) * kBarW + 0.5);

            const std::string bar = std::string(color) + str::repeat("█", static_cast<std::size_t>(filled)) + std::string(ansi::dim) + str::repeat("░", static_cast<std::size_t>(kBarW - filled)) + std::string(ansi::reset);
            push_center(bar, kBarW);
        }

        push("", 0);

        // --- cycle dots
        {
            const int done = cycle_count_ % cfg_.cycles;
            std::string dots;
            for (int i = 0; i < cfg_.cycles; ++i)
            {
                if (!dots.empty())
                    dots += ' ';
                if (i < done)
                    dots += std::string(color) + "●" + std::string(ansi::reset);
                else
                    dots += std::string(ansi::grey) + "○" + std::string(ansi::reset);
            }
            push_center(dots, static_cast<std::size_t>(cfg_.cycles) * 2 - 1);
        }

        // --- stats
        {
            std::ostringstream s;
            s << "Session " << run_focus_
              << "   ·   Today " << tmutil::format_duration(today_focus_);
            const std::string text = s.str();
            push_center(std::string(ansi::grey) + text + std::string(ansi::reset), text.size());
        }

        // --- status
        {
            std::string text;
            std::string_view col;
            if (paused_)
            {
                text = "[ PAUSED ]";
                col = ansi::yellow;
            }
            else
            {
                text = "[ RUNNING ]";
                col = ansi::green;
            }
            push_center(std::string(col) + text + std::string(ansi::reset), text.size());
        }

        push("", 0);

        // --- bottom border
        o << "  └" << str::repeat("─", kInner + 2) << "┘\n";

        // --- key hints
        o << "\n  " << ansi::dim
          << "space/p pause   s skip   r reset   +/- 1m   q detach   Q kill daemon"
          << ansi::reset << "\n";

        std::cout << o.str() << std::flush;
    }
};

// ============================================================================
//  CLI
// ============================================================================
static void print_help(const char *argv0)
{
    std::cout
        << "pomodoro — a Pomodoro timer for the terminal\n\n"
        << "Usage: " << argv0 << " [options]\n\n"
        << "Options:\n"
        << "  --work=MIN        focus duration        (default 25)\n"
        << "  --short=MIN       short break duration  (default 5)\n"
        << "  --long=MIN        long break duration   (default 15)\n"
        << "  --cycles=N        work sessions before a long break (default 4)\n"
        << "  --no-notify       disable desktop notifications\n"
        << "  --no-bell         disable terminal bell\n"
        << "  --no-log          disable session logging\n"
        << "  -h, --help        show this help\n\n"
        << "Keys while running:\n"
        << "  space / p   pause or resume\n"
        << "  s           skip to the next phase\n"
        << "  r           restart the current phase\n"
        << "  + / -       add or remove one minute\n"
        << "  q / Ctrl-C  quit\n\n"
        << "Config file : " << paths::config_file().string() << "\n"
        << "Session log : " << paths::log_file().string() << "\n";
}

static std::optional<int> int_value(std::string_view arg)
{
    const auto eq = arg.find('=');
    if (eq == std::string_view::npos)
        return std::nullopt;
    try
    {
        return std::stoi(std::string(arg.substr(eq + 1)));
    }
    catch (...)
    {
        return std::nullopt;
    }
}

int main(int argc, char **argv)
{
    bool daemon_mode = false;
    const auto cfg_path = paths::config_file();
    if (!std::filesystem::exists(cfg_path))
        Config::write_default(cfg_path);

    Config cfg = Config::load(cfg_path);

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a = argv[i];

        if (a == "-h" || a == "--help")
        {
            print_help(argv[0]);
            return 0;
        }
        else if (a.starts_with("--work="))
        {
            if (auto v = int_value(a))
                cfg.work_min = *v;
        }
        else if (a.starts_with("--short="))
        {
            if (auto v = int_value(a))
                cfg.short_break_min = *v;
        }
        else if (a.starts_with("--long="))
        {
            if (auto v = int_value(a))
                cfg.long_break_min = *v;
        }
        else if (a.starts_with("--cycles="))
        {
            if (auto v = int_value(a))
                cfg.cycles = *v;
        }
        else if (a == "--daemon") 
        { 
            daemon_mode = true; 
        }
        else if (a == "--no-notify")
        {
            cfg.notify = false;
        }
        else if (a == "--no-bell")
        {
            cfg.bell = false;
        }
        else if (a == "--no-log")
        {
            cfg.log = false;
        }
        else
        {
            std::cerr << "Unknown option: " << a << "\n";
            std::cerr << "Try '" << argv[0] << " --help'.\n";
            return 2;
        }
    }

    cfg.sanitize();

    if (daemon_is_running()) {
        std::cerr << "\n  A pomodoro daemon is already running.\n"
                  << "  Attach with:  pomo\n"
                  << "  Stop it with: pomo  (then press Q)\n\n";
        return 1;
    }

    if (daemon_mode) {
        if (!daemonize()) {
            std::cerr << "Failed to daemonize.\n";
            return 1;
        }
    }

    App app(std::move(cfg));
    app.set_daemon(daemon_mode);
    return app.run();
}