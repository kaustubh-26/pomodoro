// ============================================================================
//  pomodoro_tray_indicator.cpp — Companion tray icon for the Pomodoro timer
// ============================================================================
//
//  Build:
// g++ -std=c++20 -O2 -Wall -Wextra -o pomodoro_tray_indicator pomodoro_tray_indicator.cpp \
//     $(pkg-config --cflags --libs gtk+-3.0 appindicator3-0.1)

//  Run (in a separate terminal from pomodoro.cpp):
//    ./pomodoro_tray_indicator
//
//  Reads state from $XDG_RUNTIME_DIR/pomodoro_state.txt
//  Format: phase|remaining_seconds|paused|cycle|run_focus|today_seconds
// ============================================================================
#include <glib-unix.h>
#include <libappindicator/app-indicator.h>
#include <gtk/gtk.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// Globals
// ----------------------------------------------------------------------------
static AppIndicator* g_indicator  = nullptr;
static GtkWidget*    g_status_item = nullptr;
static GtkWidget*    g_phase_item  = nullptr;
static GtkWidget*    g_today_item  = nullptr;

// ----------------------------------------------------------------------------
// State file
// ----------------------------------------------------------------------------
static std::string state_file_path() {
    if (const char* rt = std::getenv("XDG_RUNTIME_DIR"); rt && *rt)
        return std::string(rt) + "/pomodoro_state.txt";
    return "/tmp/pomodoro_state.txt";
}

struct State {
    std::string phase   = "idle";
    int  remaining      = 0;
    bool paused         = false;
    int  cycle          = 0;
    int  run_focus      = 0;
    int  today_seconds  = 0;
    bool valid          = false;
};

static State read_state() {
    State s;
    std::ifstream f(state_file_path());
    if (!f) return s;

    std::string line;
    if (!std::getline(f, line)) return s;

    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, '|')) parts.push_back(tok);
    if (parts.size() < 3) return s;

    s.phase  = parts[0];
    try { s.remaining = std::stoi(parts[1]); } catch (...) { return s; }
    s.paused = (parts[2] == "1");
    if (parts.size() > 3) try { s.cycle         = std::stoi(parts[3]); } catch (...) {}
    if (parts.size() > 4) try { s.run_focus     = std::stoi(parts[4]); } catch (...) {}
    if (parts.size() > 5) try { s.today_seconds = std::stoi(parts[5]); } catch (...) {}
    s.valid = true;
    return s;
}

// ----------------------------------------------------------------------------
// Formatting helpers
// ----------------------------------------------------------------------------
static std::string format_time(int secs) {
    if (secs < 0) secs = 0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", secs / 60, secs % 60);
    return buf;
}

static std::string phase_display(const std::string& p) {
    if (p == "work")        return "FOCUS";
    if (p == "short_break") return "SHORT BREAK";
    if (p == "long_break")  return "LONG BREAK";
    return "IDLE";
}

static const char* icon_for(const std::string& phase, bool paused) {
    if (paused)                       return "media-playback-pause";
    if (phase == "work")              return "appointment-soon";
    if (phase == "short_break")       return "alarm";
    if (phase == "long_break")        return "alarm-symbolic";
    return "dialog-information";
}

// ----------------------------------------------------------------------------
// Tick: called every 500 ms by GLib
// ----------------------------------------------------------------------------
static gboolean on_tick(gpointer) {
    const State s = read_state();

    const std::string time_str  = s.valid ? format_time(s.remaining) : "--:--";
    const std::string phase_str = s.valid ? phase_display(s.phase)   : "IDLE";
    const std::string status    = s.paused ? "PAUSED" : phase_str;

    // Menu line 1: timer + phase
    if (g_status_item) {
        const std::string label = "⏱  " + time_str + "  —  " + status;
        gtk_menu_item_set_label(GTK_MENU_ITEM(g_status_item), label.c_str());
    }

    // Menu line 2: session / cycle
    if (g_phase_item) {
        const std::string label =
            "Session " + std::to_string(s.run_focus) +
            "   •   Cycle " + std::to_string(s.cycle);
        gtk_menu_item_set_label(GTK_MENU_ITEM(g_phase_item), label.c_str());
    }

    // Menu line 3: today's focus
    if (g_today_item) {
        const int mins = s.today_seconds / 60;
        const std::string label = "Today: " + std::to_string(mins) + " min focused";
        gtk_menu_item_set_label(GTK_MENU_ITEM(g_today_item), label.c_str());
    }

    // Icon (reflects phase / pause)
    app_indicator_set_icon_full(
        g_indicator,
        icon_for(s.phase, s.paused),
        status.c_str());

    // Try to set the label next to the icon.
    // On GNOME this is silently ignored (only KDE and some others honor it).
    app_indicator_set_label(g_indicator, time_str.c_str(), "00:00");

    return G_SOURCE_CONTINUE;
}

// ----------------------------------------------------------------------------
// Menu callbacks
// ----------------------------------------------------------------------------
static void on_quit(GtkMenuItem*, gpointer) {
    gtk_main_quit();
}

// Add this function near the other callbacks (e.g. next to on_quit)
static gboolean on_term_signal(gpointer) {
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

// ----------------------------------------------------------------------------
// main
// ----------------------------------------------------------------------------
int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    g_unix_signal_add(SIGTERM, on_term_signal, nullptr);
    g_unix_signal_add(SIGINT,  on_term_signal, nullptr);

    // ---- Build the dropdown menu -------------------------------------------
    GtkWidget* menu = gtk_menu_new();

    GtkWidget* header = gtk_menu_item_new_with_label("Pomodoro Timer");
    gtk_widget_set_sensitive(header, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), header);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    g_status_item = gtk_menu_item_new_with_label("⏱  --:--  —  IDLE");
    gtk_widget_set_sensitive(g_status_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), g_status_item);

    g_phase_item = gtk_menu_item_new_with_label("Session 0   •   Cycle 0");
    gtk_widget_set_sensitive(g_phase_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), g_phase_item);

    g_today_item = gtk_menu_item_new_with_label("Today: 0 min focused");
    gtk_widget_set_sensitive(g_today_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), g_today_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    GtkWidget* quit_item = gtk_menu_item_new_with_label("Quit tray");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_widget_show_all(menu);

    // ---- Create the indicator ----------------------------------------------
    g_indicator = app_indicator_new(
        "pomodoro-tray",
        "appointment-soon",
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

    app_indicator_set_status(g_indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_menu(g_indicator, GTK_MENU(menu));
    app_indicator_set_title(g_indicator, "Pomodoro Timer");

    // ---- Start polling -----------------------------------------------------
    g_timeout_add(500, on_tick, nullptr);
    on_tick(nullptr);   // draw immediately

    std::fprintf(stderr,
        "tray_indicator running.\n"
        "  State file : %s\n"
        "  Waiting for pomodoro.cpp to write it...\n",
        state_file_path().c_str());

    gtk_main();
    return 0;
}