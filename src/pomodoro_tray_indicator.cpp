#include <string>
#include <fstream>
#include <sstream>
#include "platform.h"

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
    #pragma comment(lib, "shell32.lib")

    #define WM_TRAYICON (WM_USER + 1)
    NOTIFYICONDATA nid = {};
    HWND hwnd = nullptr;

    std::string read_state() {
        std::ifstream f(platform::state_file());
        std::string line;
        std::getline(f, line);
        if (line.empty()) return "Pomodoro: Idle";
        // Phase|Rem|Paused|Cycle|Run|Today
        std::stringstream ss(line);
        std::string p, rem, ps;
        std::getline(ss, p, '|'); std::getline(ss, rem, '|'); std::getline(ss, ps, '|');
        int secs = std::stoi(rem);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%02d:%02d - %s", secs / 60, secs % 60, (ps == "1") ? "Paused" : "Running");
        return buf;
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_TRAYICON) {
            if (lParam == WM_RBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                std::string status = read_state();
                AppendMenu(hMenu, MF_STRING | MF_DISABLED, 0, status.c_str());
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, MF_STRING, 1, "Quit Tray");
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
        } else if (msg == WM_COMMAND) {
            if (LOWORD(wParam) == 1) PostQuitMessage(0);
        } else if (msg == WM_TIMER) {
            std::string state = read_state();
            strncpy(nid.szTip, state.c_str(), sizeof(nid.szTip) - 1);
            Shell_NotifyIcon(NIM_MODIFY, &nid);
        } else if (msg == WM_DESTROY) {
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
        WNDCLASS wc = {0}; wc.lpfnWndProc = WindowProc; wc.hInstance = hInstance;
        wc.lpszClassName = "PomodoroTray"; RegisterClass(&wc);
        hwnd = CreateWindow(wc.lpszClassName, "Tray", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = hwnd; nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        strcpy(nid.szTip, "Pomodoro Timer");
        Shell_NotifyIcon(NIM_ADD, &nid);

        SetTimer(hwnd, 1, 500, NULL);
        MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        return 0;
    }
#else
    #include <gtk/gtk.h>
    #include <libappindicator/app-indicator.h>
    #include <glib-unix.h>

    static AppIndicator* g_indicator = nullptr;
    static GtkWidget* g_status_item = nullptr;

    static gboolean on_tick(gpointer) {
        std::ifstream f(platform::state_file());
        std::string line;
        if (std::getline(f, line)) {
            // Extract basic info
            std::stringstream ss(line);
            std::string p, rem, ps;
            std::getline(ss, p, '|'); std::getline(ss, rem, '|'); std::getline(ss, ps, '|');
            int secs = rem.empty() ? 0 : std::stoi(rem);
            char time_buf[16]; std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d", secs / 60, secs % 60);
            
            std::string label = "⏱  " + std::string(time_buf);
            if (ps == "1") label += " (Paused)";
            
            gtk_menu_item_set_label(GTK_MENU_ITEM(g_status_item), label.c_str());
            app_indicator_set_label(g_indicator, time_buf, "00:00");
        }
        return G_SOURCE_CONTINUE;
    }

    static gboolean on_term(gpointer) { gtk_main_quit(); return G_SOURCE_REMOVE; }

    int main(int argc, char** argv) {
        gtk_init(&argc, &argv);
        g_unix_signal_add(SIGTERM, on_term, nullptr);
        
        GtkWidget* menu = gtk_menu_new();
        g_status_item = gtk_menu_item_new_with_label("--:--");
        gtk_widget_set_sensitive(g_status_item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), g_status_item);
        gtk_widget_show_all(menu);

        g_indicator = app_indicator_new("pomodoro-tray", "appointment-soon", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
        app_indicator_set_status(g_indicator, APP_INDICATOR_STATUS_ACTIVE);
        app_indicator_set_menu(g_indicator, GTK_MENU(menu));

        g_timeout_add(500, on_tick, nullptr);
        on_tick(nullptr);
        gtk_main();
        return 0;
    }
#endif