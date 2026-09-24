CXX      := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra
PREFIX   ?= $(HOME)/.local
LIBDIR   := $(PREFIX)/lib/pomodoro
BINDIR   := $(PREFIX)/bin
SRCDIR   := src
SOUNDDIR := sounds

GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
APP_CFLAGS := $(shell pkg-config --cflags appindicator3-0.1 2>/dev/null || \
                     pkg-config --cflags ayatana-appindicator3-0.1 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs gtk+-3.0 2>/dev/null)
APP_LIBS   := $(shell pkg-config --libs appindicator3-0.1 2>/dev/null || \
                     pkg-config --libs ayatana-appindicator3-0.1 2>/dev/null)

TRAY_CFLAGS := $(GTK_CFLAGS) $(APP_CFLAGS)
TRAY_LIBS   := $(GTK_LIBS)   $(APP_LIBS)

.PHONY: all clean install uninstall check-deps

all: pomodoro pomo pomodoro_tray_indicator

pomodoro: $(SRCDIR)/pomodoro.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

pomo: $(SRCDIR)/pomo.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

pomodoro_tray_indicator: $(SRCDIR)/pomodoro_tray_indicator.cpp
	$(CXX) $(CXXFLAGS) $(TRAY_CFLAGS) -o $@ $< $(TRAY_LIBS)

install: all
	mkdir -p $(LIBDIR)/sounds $(BINDIR)
	cp pomodoro pomo pomodoro_tray_indicator $(LIBDIR)/
	cp $(SOUNDDIR)/*.wav $(LIBDIR)/sounds/ 2>/dev/null || true
	ln -sf $(LIBDIR)/pomodoro                $(BINDIR)/pomodoro
	ln -sf $(LIBDIR)/pomo                    $(BINDIR)/pomo
	ln -sf $(LIBDIR)/pomodoro_tray_indicator $(BINDIR)/pomodoro_tray_indicator
	@echo ""
	@echo "Installed to $(LIBDIR)"
	@echo "Symlinks in $(BINDIR)"

uninstall:
	rm -f $(BINDIR)/pomodoro $(BINDIR)/pomo $(BINDIR)/pomodoro_tray_indicator
	rm -rf $(LIBDIR)

clean:
	rm -f pomodoro pomo pomodoro_tray_indicator