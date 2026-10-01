CXX ?= g++
HYPRLAND_SRC ?=

TARGET = bubblemv.so
SRC = src/main.cpp

CXXFLAGS += -shared -fPIC -std=c++2b -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
INCLUDES = $(shell pkg-config --cflags --keep-system-cflags hyprland 2>/dev/null)
ifneq ($(strip $(HYPRLAND_SRC)),)
INCLUDES += -I$(HYPRLAND_SRC) -I$(HYPRLAND_SRC)/protocols
endif

# Hyprland embeds Lua 5.5; plugins must compile against the same headers.
# The module names and version range match Hyprland's own CMake lookup.
LUA_PKG := $(firstword $(foreach m,lua55 lua5.5 lua-55 lua-5.5,$(shell pkg-config --exists $(m) 2>/dev/null && echo $(m))) \
	$(shell pkg-config --exists 'lua >= 5.5' 'lua < 5.6' 2>/dev/null && echo lua))
LUA_CFLAGS := $(if $(LUA_PKG),$(shell pkg-config --cflags $(LUA_PKG)))
LUA_LIBS := $(if $(LUA_PKG),$(shell pkg-config --libs $(LUA_PKG)))
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
	@test -n "$(LUA_PKG)" || \
		(printf 'Missing Lua 5.5 development files (pkg-config module lua55, lua5.5, lua-55, lua-5.5 or lua 5.5.x).\n' >&2; exit 1)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) $< -o $@ $(PKG_LIBS)

clean:
	rm -f $(TARGET)
