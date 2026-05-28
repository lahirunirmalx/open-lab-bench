# Open LabBench — SDL2 application
# Build deps (Ubuntu/Debian): libsdl2-dev libsdl2-ttf-dev
#
# Common invocations:
#   make                       # build psu_app, psu_probe, and legacy GUIs
#   make app                   # only the new single-binary app
#   make probe                 # only the CLI sanity tool
#   make legacy                # only the four legacy GUIs
#   make run                   # build + run psu_app against the demo driver
#   make run ARGS="--driver=modbus-bridge --view=toolbar-single --port=/dev/ttyUSB0"
#   make clean                 # remove build/ and all binaries
#   make install               # install psu_app to $(PREFIX)/bin  (default /usr/local)
#   sudo make install PREFIX=/usr

CC      ?= gcc
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
BUILD   ?= build

# Prefer pkg-config so the build picks up the right include/lib paths on any
# distro. Fall back to plain -l flags if pkg-config can't see SDL2.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs   sdl2 SDL2_ttf 2>/dev/null)
ifeq ($(strip $(SDL_LIBS)),)
SDL_LIBS := -lSDL2 -lSDL2_ttf
endif

# Header-search paths for the new tree.
#   include/        — public driver interface (psu_driver.h)
#   src/            — "drivers/foo.h", "views/foo.h" style includes
#   src/transport/  — serial_port.h, shared by drivers
NEW_INCLUDES := -Iinclude -Isrc -Isrc/transport

CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -pthread $(SDL_CFLAGS)
LDFLAGS += -pthread
LDLIBS  += $(SDL_LIBS) -lm

# ----- New tree -----------------------------------------------------------

TRANSPORT_SRCS := \
    src/transport/serial_port.c \
    src/transport/scpi.c \
    src/transport/scpi_serial.c \
    src/transport/scpi_prologix.c

DRIVER_SRCS := \
    src/drivers/registry.c \
    src/drivers/demo.c \
    src/drivers/dmm_demo.c \
    src/drivers/dmm_helpers.c \
    src/drivers/modbus_bridge/modbus_bridge.c \
    src/drivers/modbus_bridge/psu_protocol.c \
    src/drivers/scpi_psu/scpi_psu.c \
    src/drivers/korad/korad.c \
    src/drivers/owon_xdm/owon_xdm.c \
    src/drivers/scpi_dmm/scpi_dmm.c

VIEW_SRCS := \
    src/views/registry.c \
    src/views/vfd_dotmatrix.c \
    src/views/toolbar_single.c \
    src/views/toolbar_dual.c \
    src/views/full_common.c \
    src/views/full_dual.c \
    src/views/full_single.c \
    src/views/dmm_toolbar.c \
    src/views/dmm_full.c

# ----- Binaries -----------------------------------------------------------

APP_BINS    := psu_app
TOOL_BINS   := psu_probe
LEGACY_BINS := psu_gui psu_gui_single psu_gui_toolbar psu_gui_toolbar_single
BINS        := $(APP_BINS) $(TOOL_BINS) $(LEGACY_BINS)

# Per-binary source lists.
psu_app_SRCS   := src/app/psu_app.c src/app/launcher.c \
                  $(TRANSPORT_SRCS) $(DRIVER_SRCS) $(VIEW_SRCS)
psu_probe_SRCS := src/app/psu_probe.c $(TRANSPORT_SRCS) $(DRIVER_SRCS)

# psu_probe doesn't need SDL/TTF.
psu_probe_LDLIBS  := -pthread -lm
psu_probe_CPPFLAGS := $(NEW_INCLUDES)
psu_app_CPPFLAGS   := $(NEW_INCLUDES)

# Legacy four GUIs — self-contained in legacy/ with their own copies of
# psu_protocol/serial_port. They get removed as views are ported in Phase 3.
LEGACY_INC := -Ilegacy

psu_gui_SRCS                := legacy/main.c                legacy/serial_port.c legacy/psu_protocol.c
psu_gui_single_SRCS         := legacy/main_single.c         legacy/serial_port.c legacy/psu_protocol.c
psu_gui_toolbar_SRCS        := legacy/main_toolbar.c        legacy/serial_port.c legacy/psu_protocol.c
psu_gui_toolbar_single_SRCS := legacy/main_toolbar_single.c legacy/serial_port.c legacy/psu_protocol.c

psu_gui_CPPFLAGS                := $(LEGACY_INC)
psu_gui_single_CPPFLAGS         := $(LEGACY_INC)
psu_gui_toolbar_CPPFLAGS        := $(LEGACY_INC)
psu_gui_toolbar_single_CPPFLAGS := $(LEGACY_INC)

# ----- Build rules --------------------------------------------------------

.PHONY: all app probe legacy clean install uninstall run
.DEFAULT_GOAL := all

all:    $(BINS)
app:    $(APP_BINS)
probe:  $(TOOL_BINS)
legacy: $(LEGACY_BINS)

# Each binary gets its own $(BUILD)/<bin>/ subtree mirroring source paths, so
# objects for the same .c compiled with different flags don't collide.
define BUILD_template
$(1)_OBJS := $$(patsubst %.c,$(BUILD)/$(1)/%.o,$$($(1)_SRCS))

$$($(1)_OBJS): $(BUILD)/$(1)/%.o: %.c
	@mkdir -p $$(@D)
	$$(CC) $$($(1)_CPPFLAGS) $$(CFLAGS) -c $$< -o $$@

$(1): $$($(1)_OBJS)
	$$(CC) $$(LDFLAGS) $$^ -o $$@ $$(if $$($(1)_LDLIBS),$$($(1)_LDLIBS),$$(LDLIBS))
endef
$(foreach bin,$(BINS),$(eval $(call BUILD_template,$(bin))))

run: psu_app
	./psu_app $(if $(ARGS),$(ARGS),--driver=demo --view=toolbar-single --port=-)

install: $(APP_BINS)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(APP_BINS) $(DESTDIR)$(BINDIR)/

uninstall:
	cd $(DESTDIR)$(BINDIR) && rm -f $(APP_BINS)

clean:
	rm -rf $(BUILD) $(BINS)
