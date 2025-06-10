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

VPATH += src
VPATH += peanut_gb
VPATH += minigb_apu

# List C source files here
SRC += src/version.c
SRC += src/game_scene.c
SRC += src/userstack.c
SRC += src/jparse.c
SRC += src/script.c
SRC += src/dtcm.c
SRC += src/revcheck.c
SRC += src/app.c
SRC += src/utility.c
SRC += src/scene.c
SRC += src/library_scene.c
SRC += src/array.c
SRC += src/listview.c
SRC += src/preferences.c
SRC += src/settings_scene.c

SRC += minigb_apu/minigb_apu.c
SRC += main.c
SRC += pdnewlib.c

SRC += lua-5.4.7/onelua.c

ASRC = setup.s

# List all user directories here
UINCDIR += src
UINCDIR += peanut_gb
UINCDIR += minigb_apu
UINCDIR += lcd
UINCDIR += lua-5.4.7


# Note: if there are unexplained crashes, try disabling these.
# LUA: enable lua scripting (uses embedded lua engine)
# DTCM_ALLOC: allow allocating variables in DTCM at the low-address end of the region reserved for the stack.
# ITCM_CORE (requires DTCM_ALLOC, and special link_map.ld): run core interpreter from ITCM.
# NOLUA: disable lua support
# Note: DTCM only active on Rev A regardless.
UDEFS = -DDTCM_ALLOC -DITCM_CORE -DDTCM_DEBUG=0 -falign-loops=32 -fprefetch-loop-arrays

# Define ASM defines here
UADEFS =

# List the user directory to look for the libraries here
ULIBDIR =

# List all user libraries here
ULIBS =

override LDSCRIPT=./link_map.ld

include $(SDK)/C_API/buildsupport/common.mk
