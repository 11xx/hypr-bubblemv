CXX ?= g++
HYPRLAND_SRC ?=

TARGET = hypr-bubblemv.so
SRC = src/main.cpp

CXXFLAGS += -shared -fPIC -std=c++2b -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
INCLUDES =
ifneq ($(strip $(HYPRLAND_SRC)),)
INCLUDES += -I$(HYPRLAND_SRC) -I$(HYPRLAND_SRC)/protocols
endif

PKG_CONFIG_DEPS = hyprland pixman-1 libdrm pangocairo libinput libudev wayland-server xkbcommon hyprlang
PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_CONFIG_DEPS) 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs pangocairo 2>/dev/null)

.PHONY: all clean check-hyprland-src check-deps

all: check-hyprland-src check-deps $(TARGET)

check-hyprland-src:
	@if test -n "$(HYPRLAND_SRC)"; then test -f "$(HYPRLAND_SRC)/src/plugins/PluginAPI.hpp" || \
		(printf 'HYPRLAND_SRC must point to a Hyprland source tree. Current: %s\n' "$(HYPRLAND_SRC)" >&2; exit 1); \
	fi

check-deps:
	@pkg-config --exists $(PKG_CONFIG_DEPS) || \
		(printf 'Missing build dependencies. pkg-config could not find: %s\n' "$(PKG_CONFIG_DEPS)" >&2; exit 1)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(PKG_CFLAGS) $< -o $@ $(PKG_LIBS)

clean:
	rm -f $(TARGET)
