#pragma once

#include <filesystem>
#include <string>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <algorithm>
#include <cstdio> // FIX: Required for std::snprintf
#include <ctime>  // FIX: Required for std::time_t / std::tm

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <conio.h>
    #include <io.h>
    #include <mmsystem.h>
    #pragma comment(lib, "winmm.lib")
#else
    #include <poll.h>
    #include <termios.h>
    #include <unistd.h>
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <sys/prctl.h>
    #include <sys/wait.h>
    #include <csignal>
    #include <fcntl.h>
    #include <sys/stat.h>
#endif

namespace platform {

// --- Paths ---
inline std::filesystem::path get_exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : p.parent_path();
#endif
}

inline std::filesystem::path get_config_dir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    return std::filesystem::path(appdata ? appdata : ".") / "pomodoro";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg) return std::filesystem::path(xdg) / "pomodoro";
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".config" / "pomodoro";
#endif
}

inline std::filesystem::path get_data_dir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    return std::filesystem::path(appdata ? appdata : ".") / "pomodoro" / "data";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg) return std::filesystem::path(xdg) / "pomodoro";
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".local" / "share" / "pomodoro";
#endif
}

inline std::filesystem::path get_runtime_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetTempPathA(MAX_PATH, buf);
    return std::filesystem::path(buf);
#else
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    return rt ? std::filesystem::path(rt) : std::filesystem::path("/tmp");
#endif
}

inline std::filesystem::path config_file() { return get_config_dir() / "config"; }
inline std::filesystem::path log_file() { return get_data_dir() / "sessions.csv"; }
inline std::filesystem::path state_file() { return get_runtime_dir() / "pomodoro_state.txt"; }
inline std::filesystem::path socket_file() { return get_runtime_dir() / "pomodoro.sock"; }

// --- Time ---
inline void localtime_safe(const std::time_t* t, std::tm* tmv) {
#ifdef _WIN32
    localtime_s(tmv, t);
#else
    localtime_r(t, tmv);
#endif
}

// --- Terminal Raw Mode ---
class RawTerminal {
public:
    RawTerminal() {
#ifdef _WIN32
        hStdin = GetStdHandle(STD_INPUT_HANDLE);
        hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleMode(hStdin, &old_in);
        GetConsoleMode(hStdout, &old_out);
        SetConsoleMode(hStdin, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT);
        SetConsoleMode(hStdout, ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
        std::cout << "\033[?25l" << std::flush; // hide cursor
#else
        if (::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &old_t) == 0) {
            termios raw = old_t;
            raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) active = true;
        }
        std::cout << "\033[?25l" << std::flush;
#endif
    }
    ~RawTerminal() {
#ifdef _WIN32
        SetConsoleMode(hStdin, old_in);
        SetConsoleMode(hStdout, old_out);
        std::cout << "\033[?25h\033[0m" << std::flush;
#else
        if (active) ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_t);
        std::cout << "\033[?25h\033[0m" << std::flush;
#endif
    }
    RawTerminal(const RawTerminal&) = delete;
    RawTerminal& operator=(const RawTerminal&) = delete;
private:
#ifdef _WIN32
    HANDLE hStdin, hStdout;
    DWORD old_in, old_out;
#else
    termios old_t;
    bool active = false;
#endif
};

// --- Input Polling ---
inline bool wait_for_input(int timeout_ms) {
#ifdef _WIN32
    DWORD start = GetTickCount();
    while (GetTickCount() - start < (DWORD)timeout_ms) {
        if (_kbhit()) return true;
        Sleep(10);
    }
    return _kbhit() != 0;
#else
    struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
    return ::poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN);
#endif
}

inline char read_char() {
#ifdef _WIN32
    return _getch();
#else
    char c = 0;
    if (::read(STDIN_FILENO, &c, 1) < 0) return 0;
    return c;
#endif
}

// --- Daemonize ---
inline bool daemonize() {
#ifdef _WIN32
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::string cmd_line = std::string("\"") + exe_path + "\" --daemon";
    if (CreateProcessA(NULL, cmd_line.data(), NULL, NULL, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        return true; 
    }
    return false;
#else
    pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid > 0) std::exit(0);
    if (::setsid() < 0) return false;
    std::signal(SIGHUP, SIG_IGN);
    pid = ::fork();
    if (pid < 0) return false;
    if (pid > 0) std::exit(0);
    ::umask(0);
    if (::chdir("/") < 0) return false;
    const int fd = ::open("/dev/null", O_RDWR);
    if (fd < 0) return false;
    ::dup2(fd, STDIN_FILENO); ::dup2(fd, STDOUT_FILENO); ::dup2(fd, STDERR_FILENO);
    if (fd > 2) ::close(fd);
    return true;
#endif
}

inline bool fork_to_background() {
#ifdef _WIN32
    std::cout << "\n  Detached. Pomodoro running in the background.\n"
              << "  Re-attach with: pomo\n\n" << std::flush;
    if (daemonize()) {
        std::exit(0);
    }
    return false;
#else
    std::cout << "\n  Detached. Pomodoro running in the background.\n"
              << "  Re-attach with: pomo\n\n" << std::flush;
    return daemonize();
#endif
}

inline bool daemon_is_running() {
#ifdef _WIN32
    // FIX: Correctly differentiate between 'busy' (running) and 'not found' (not running)
    if (WaitNamedPipeA("\\\\.\\pipe\\pomodoro", 0)) return true;
    return GetLastError() == ERROR_PIPE_BUSY;
#else
    const auto sp = socket_file();
    if (!std::filesystem::exists(sp)) return false;
    const int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sp.c_str());
    const bool ok = (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    ::close(s);
    return ok;
#endif
}

// --- Notifications & Sound ---
inline void play_sound(const std::string& path) {
#ifdef _WIN32
    PlaySoundA(path.c_str(), NULL, SND_FILENAME | SND_ASYNC);
#else
    std::string cmd = "paplay '" + path + "' >/dev/null 2>&1 &";
    if (std::system(cmd.c_str()) != 0) {}
#endif
}

inline void notify(const std::string& title, const std::string& body) {
#ifdef _WIN32
    MessageBeep(MB_ICONASTERISK);
#else
    std::string cmd = "notify-send -a Pomodoro -t 6000 '" + title + "' '" + body + "' >/dev/null 2>&1 &";
    if (std::system(cmd.c_str()) != 0) {}
#endif
}

// --- IPC (Client) ---
class IPCClient {
public:
    bool send(const std::string& cmd) {
#ifdef _WIN32
        HANDLE hPipe = CreateFileA("\\\\.\\pipe\\pomodoro", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPipe == INVALID_HANDLE_VALUE) return false;
        DWORD written; WriteFile(hPipe, cmd.c_str(), cmd.size(), &written, NULL);
        CloseHandle(hPipe); return true;
#else
        int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (s < 0) return false;
        sockaddr_un addr{}; addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_file().c_str());
        if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { ::close(s); return false; }
        std::string line = cmd + "\n";
        // FIX: Use MSG_NOSIGNAL to prevent SIGPIPE if the daemon dies
        ssize_t n = ::send(s, line.c_str(), line.size(), MSG_NOSIGNAL); (void)n;
        ::close(s); return true;
#endif
    }
};

// --- IPC (Server) ---
class IPCServer {
public:
    bool init() {
#ifdef _WIN32
        hPipe = CreateNamedPipeA("\\\\.\\pipe\\pomodoro", PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT, 1, 0, 0, 0, NULL);
        return hPipe != INVALID_HANDLE_VALUE;
#else
        ::unlink(socket_file().c_str());
        listen_sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_sock < 0) return false;
        sockaddr_un addr{}; addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_file().c_str());
        if (::bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { ::close(listen_sock); return false; }
        if (::listen(listen_sock, 4) < 0) { ::close(listen_sock); return false; }
        int flags = ::fcntl(listen_sock, F_GETFL, 0);
        ::fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK);
        return true;
#endif
    }

    std::string poll_command() {
#ifdef _WIN32
        if (!hPipe || hPipe == INVALID_HANDLE_VALUE) return "";
        if (!ConnectNamedPipe(hPipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) return "";
        char buf[64] = {};
        DWORD bytesRead;
        if (ReadFile(hPipe, buf, sizeof(buf) - 1, &bytesRead, NULL)) {
            DisconnectNamedPipe(hPipe);
            return std::string(buf, bytesRead);
        }
        return "";
#else
        if (listen_sock < 0) return "";
        struct pollfd lp = {listen_sock, POLLIN, 0};
        if (::poll(&lp, 1, 0) <= 0 || !(lp.revents & POLLIN)) return "";
        
        int client = ::accept(listen_sock, nullptr, nullptr);
        if (client < 0) return "";
        
        struct pollfd cp = {client, POLLIN, 0};
        if (::poll(&cp, 1, 100) <= 0 || !(cp.revents & POLLIN)) { ::close(client); return ""; }
        
        char buf[64] = {};
        ssize_t n = ::read(client, buf, sizeof(buf) - 1);
        ::close(client);
        if (n > 0) return std::string(buf, n);
        return "";
#endif
    }

    void cleanup() {
#ifdef _WIN32
        if (hPipe && hPipe != INVALID_HANDLE_VALUE) CloseHandle(hPipe);
#else
        if (listen_sock >= 0) ::close(listen_sock);
        ::unlink(socket_file().c_str());
#endif
    }
private:
#ifdef _WIN32
    HANDLE hPipe = INVALID_HANDLE_VALUE;
#else
    int listen_sock = -1;
#endif
};

} // namespace platform