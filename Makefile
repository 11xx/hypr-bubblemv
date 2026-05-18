CXX ?= g++
HYPRLAND_SRC ?=

TARGET = hypr-bubblemv.so
SRC = src/main.cpp

CXXFLAGS += -shared -fPIC -std=c++2b -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
INCLUDES = $(shell pkg-config --cflags --keep-system-cflags hyprland 2>/dev/null)
ifneq ($(strip $(HYPRLAND_SRC)),)
INCLUDES += -I$(HYPRLAND_SRC) -I$(HYPRLAND_SRC)/protocols
endif

LUA_CFLAGS := $(shell pkg-config --cflags lua 2>/dev/null || pkg-config --cflags lua5.4 2>/dev/null || pkg-config --cflags lua-5.4 2>/dev/null)
LUA_LIBS := $(shell pkg-config --libs lua 2>/dev/null || pkg-config --libs lua5.4 2>/dev/null || pkg-config --libs lua-5.4 2>/dev/null)
PKG_CFLAGS := $(INCLUDES) $(LUA_CFLAGS)
PKG_LIBS := $(LUA_LIBS)

.PHONY: all clean check-hyprland-src check-deps

all: check-hyprland-src check-deps $(TARGET)

check-hyprland-src:
	@if test -n "$(HYPRLAND_SRC)"; then test -f "$(HYPRLAND_SRC)/src/plugins/PluginAPI.hpp" || \
		(printf 'HYPRLAND_SRC must point to a Hyprland source tree. Current: %s\n' "$(HYPRLAND_SRC)" >&2; exit 1); \
	fi

check-deps:
	@if test -z "$(HYPRLAND_SRC)"; then pkg-config --exists hyprland || \
		(printf 'Missing Hyprland headers. Run hyprpm update, or set PKG_CONFIG_PATH to a directory containing hyprland.pc.\n' >&2; exit 1); \
	fi

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) $< -o $@ $(PKG_LIBS)

clean:
	rm -f $(TARGET)
