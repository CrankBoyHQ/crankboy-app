HEAP_SIZE      = 8388208
STACK_SIZE     = 61800

PRODUCT = CrankBoy.pdx

SDK = ${PLAYDATE_SDK_PATH}
ifeq ($(SDK),)
	SDK = $(shell egrep '^\s*SDKRoot' ~/.Playdate/config | head -n 1 | cut -c9-)
endif

ifeq ($(SDK),)
	$(error SDK path not found; set ENV value PLAYDATE_SDK_PATH)
endif

# --- COPY LUA SCRIPTS ---
COPY_LUA_SCRIPTS := $(shell \
	mkdir -p Source/scripts; \
	for file in src/scripts/*.lua; do \
		if [ -e "$$file" ]; then \
			base=$$(basename "$$file" .lua); \
			cp "$$file" "Source/scripts/$${base}.l"; \
		fi; \
	done; \
)

VPATH += src
VPATH += libs/minigb_apu
VPATH += libs/lz4
VPATH += libs/lua-5.4.7
VPATH += libs/pdnewlib
VPATH += libs

# collect C scripts
SRC += $(wildcard src/cscripts/*.c)

# List C source files here
SRC += src/app.c
SRC += src/array.c
SRC += src/dtcm.c
SRC += src/http.c
SRC += src/jparse.c
SRC += src/listview.c
SRC += src/pgmusic.c
SRC += src/preferences.c
SRC += src/revcheck.c
SRC += src/scene.c
SRC += src/scenes/cover_cache_scene.c
SRC += src/scenes/credits_scene.c
SRC += src/scenes/file_copying_scene.c
SRC += src/scenes/game_scanning_scene.c
SRC += src/scenes/game_scene.c
SRC += src/scenes/image_conversion_scene.c
SRC += src/scenes/info_scene.c
SRC += src/scenes/library_scene.c
SRC += src/scenes/modal.c
SRC += src/scenes/patches_scene.c
SRC += src/scenes/settings_scene.c
SRC += src/script.c
SRC += src/scriptutil.c
SRC += src/softpatch.c
SRC += src/userstack.c
SRC += src/utility.c
SRC += src/version.c

SRC += libs/minigb_apu/minigb_apu.c
SRC += libs/pdnewlib/pdnewlib.c
SRC += main.c

SRC += libs/lua-5.4.7/onelua.c
SRC += libs/lz4/lz4.c

ASRC = setup.s

# List all user directories here
UINCDIR += src
UINCDIR += libs
UINCDIR += libs/minigb_apu
UINCDIR += libs/lz4
UINCDIR += libs/lua-5.4.7
UINCDIR += libs/pdnewlib

# Note: if there are unexplained crashes, try disabling these.
# LUA: enable lua scripting (uses embedded lua engine)
# DTCM_ALLOC: allow allocating variables in DTCM at the low-address end of the region reserved for the stack.
# ITCM_CORE (requires DTCM_ALLOC, and special link_map.ld): run core interpreter from ITCM.
# NOLUA: disable lua support
# Note: DTCM only active on Rev A regardless.
# -fstack-usage: Add this to measure the stack usage (only for debugging)
UDEFS = -DDTCM_ALLOC -DITCM_CORE -DDTCM_DEBUG=0 -falign-loops=32 -fprefetch-loop-arrays

# Define ASM defines here
UADEFS =

# List the user directory to look for the libraries here
ULIBDIR =

# List all user libraries here
ULIBS =

override LDSCRIPT=./link_map.ld

include $(SDK)/C_API/buildsupport/common.mk

# Update pdxinfo from version.json (unless bundle.json present)
VERSION_JSON := Source/version.json
PDXINFO := Source/pdxinfo
PYTHON := $(shell command -v python3b 2>/dev/null)

ifneq ("$(wildcard Source/bundle.json)","")
ifdef PYTHON
    $(shell python3 scripts/update_version.py Source/pdxinfo Source/version.json Source/pdxinfo)
else
    $(info WARNING: python3 required to update pdxinfo from version.json)
endif
endif

PDCFLAGS += --quiet

# --- CUSTOM CLEANUP ---
.PHONY: clean-scripts
clean-scripts:
	@echo "Cleaning up Lua scripts..."
	@rm -rf Source/scripts

clean: clean-scripts
