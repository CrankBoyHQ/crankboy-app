HEAP_SIZE      = 8388208
STACK_SIZE     = 61800

PRODUCT = CrankBoy.pdx

# Note: to rebuild db/*.json database, run $(PYTHON) scripts/create_rom_list.py

SDK = ${PLAYDATE_SDK_PATH}
ifeq ($(SDK),)
	SDK = $(shell egrep '^\s*SDKRoot' ~/.Playdate/config | head -n 1 | cut -c9-)
endif

ifeq ($(SDK),)
	$(error SDK path not found; set ENV value PLAYDATE_SDK_PATH)
endif


VPATH += src
VPATH += libs/minigb_apu
VPATH += libs/lz4
VPATH += libs/miniz
VPATH += libs/pdnewlib
VPATH += libs

# collect C scripts
SRC += $(wildcard src/cscripts/*.c)

# List C source files here
SRC += src/app.c
SRC += src/crankemu.c
SRC += src/scenes/emucore_game_scene.c
SRC += src/scenes/parental_lock_scene.c
SRC += src/scenes/game_scene.c
SRC += src/scenes/settings_scene.c
SRC += src/scenes/homebrew_hub_scene.c
SRC += src/http_safe.c
SRC += src/global.c
SRC += src/serial.c
SRC += src/ft.c
SRC += src/array.c
SRC += src/dtcm.c
SRC += src/http.c
SRC += src/jparse.c
SRC += src/listview.c
SRC += src/pgmusic.c
SRC += src/preferences.c
SRC += src/emucore_prefs.c
SRC += src/recommended_json.c
SRC += src/revcheck.c
SRC += src/scene.c
SRC += src/scenes/credits_scene.c
SRC += src/scenes/file_copying_scene.c
SRC += src/scenes/patch_download_scene.c
SRC += src/scenes/game_scanning_scene.c
SRC += src/scenes/library_scene.c
SRC += src/scenes/manage_rom_scene.c
SRC += src/scenes/image_conversion_scene.c
SRC += src/scenes/info_scene.c
SRC += src/scenes/modal.c
SRC += src/scenes/patches_scene.c
SRC += src/scenes/sft_modal.c
SRC += src/script.c
SRC += src/scriptutil.c
SRC += src/softpatch.c
SRC += src/userstack.c
SRC += src/utility.c
SRC += src/version.c
SRC += src/gbz.c

SRC += libs/minigb_apu/minigb_apu.c
SRC += libs/pdnewlib/pdnewlib.c
SRC += main.c

SRC += libs/lz4/lz4.c
SRC += libs/miniz/miniz.c
SRC += libs/miniz/mini_gzip.c
SRC += libs/nayuki/qrcodegen.c

SRC += libs/pdll/pdll.c
SRC += libs/pdll/uzlib/tinflate.c
SRC += libs/pdll/uzlib/tinfzlib.c
SRC += libs/pdll/uzlib/adler32.c
SRC += libs/pdll/uzlib/crc32.c

# Baked data files (generated from Source/*.json; see rules below)
SRC += build/baked_version_json.c
SRC += build/baked_credits_json.c
SRC += build/baked_en_strings.c
SRC += build/baked_ja_strings.c
SRC += src/l10n/l10n.c

ASRC = setup.s

# List all user directories here
UINCDIR += src
UINCDIR += libs
UINCDIR += libs/minigb_apu
UINCDIR += libs/lz4
UINCDIR += libs/miniz
UINCDIR += libs/pdnewlib

# (device-only flags)

# Note: if there are unexplained crashes, try disabling these.
# DTCM_ALLOC: allow allocating variables in DTCM at the low-address end of the region reserved for the stack.
# ITCM_CORE (requires DTCM_ALLOC, and special link_map.ld): run core interpreter from ITCM.
# Note: DTCM only active on Rev A regardless.
# DTCM_DEBUG: set >0 to verify DTCM canary on every allocation (slow, debug only).
# -fstack-usage: Add this to measure the stack usage (only for debugging)
UDEFS += -DDTCM_ALLOC -DITCM_CORE -DDTCM_DEBUG=0 -DLZ4_MEMORY_USAGE=12 -falign-loops=32 -fprefetch-loop-arrays

# flags applied to simulator only
SIMULATOR_FLAGS +=

# flags applied to both simulator and device
COMMON_FLAGS +=

# Define ASM defines here
UADEFS =

# List the user directory to look for the libraries here
ULIBDIR =

# List all user libraries here
ULIBS =

override LDSCRIPT=./link_map.ld

include $(SDK)/C_API/buildsupport/common.mk

# Auto-detect Python: use project venv if present, otherwise system python3
PYTHON := $(shell \
    if [ -x .venv/bin/python3 ]; then \
        echo .venv/bin/python3; \
    else \
        command -v python3 2>/dev/null || echo python3; \
    fi)

# Update pdxinfo from version.json (unless bundle.json present)
ifneq ("$(wildcard Source/bundle.json)","")
    $(shell $(PYTHON) scripts/update_version.py Source/pdxinfo Source/version.json Source/pdxinfo)
endif

# Fail early if Python deps missing (blocks parallel C compilation)
.PHONY: check_python_deps
check_python_deps:
	@$(PYTHON) -c "import fontTools; from PIL import Image" 2>/dev/null || { \
	    if [ ! -d .venv ]; then \
	        echo "ERROR: Python deps missing (fonttools, Pillow)."; \
	        echo ""; \
	        echo "First-time setup:"; \
	        echo "  python3 -m venv .venv"; \
	        echo "  .venv/bin/pip install -r scripts/requirements.txt"; \
	    else \
	        echo "ERROR: Python deps missing in .venv (fonttools, Pillow)."; \
	        echo ""; \
	        echo "Run:"; \
	        echo "  .venv/bin/pip install -r scripts/requirements.txt"; \
	    fi; \
	    echo ""; \
	    exit 1; \
	}

# Make device and simulator build depend on Python dep check
device_bin: check_python_deps
simulator_bin: check_python_deps

PDCFLAGS += --quiet

# bake Source/*.json into C source
build/baked_%_json.c: Source/%.json scripts/embed_json.py | MKOBJDIR
	$(PYTHON) scripts/embed_json.py $< $@ baked_$*_json

build/baked_%_strings.c: src/l10n/%.strings | MKOBJDIR
	xxd -i -n baked_$*_strings $< | sed 's/^unsigned/const unsigned/' > $@

JP_TTF  = assets/fonts/LINESeed/LINESeedJP-Bold.ttf
JP_LIST = build/glyphs-jp.txt

$(JP_LIST): src/l10n/ja.strings scripts/list_jp_glyphs.py | MKOBJDIR
	$(PYTHON) scripts/list_jp_glyphs.py src/l10n/ja.strings $@

FONT_STAMP = build/fonts.stamp

$(FONT_STAMP): $(JP_LIST) $(JP_TTF) scripts/insert_glyphs.py $(wildcard assets/fonts/*.fnt assets/fonts/*.png) | MKOBJDIR
	@$(PYTHON) -c "import fontTools; from PIL import Image" 2>/dev/null || { \
	    if [ ! -d .venv ]; then \
	        echo "ERROR: Python deps missing (fonttools, Pillow)."; \
	        echo ""; \
	        echo "First-time setup:"; \
	        echo "  python3 -m venv .venv"; \
	        echo "  .venv/bin/pip install -r scripts/requirements.txt"; \
	    else \
	        echo "ERROR: Python deps missing in .venv (fonttools, Pillow)."; \
	        echo ""; \
	        echo "Run:"; \
	        echo "  .venv/bin/pip install -r scripts/requirements.txt"; \
	    fi; \
	    echo ""; \
	    exit 1; \
	}
	cp assets/fonts/*.fnt assets/fonts/*.png Source/fonts/
	$(PYTHON) scripts/insert_glyphs.py $(JP_LIST) $(JP_TTF) Source/fonts/Roobert-11-Medium-table-22-22.png Source/fonts/Roobert-11-Medium.fnt --beta-gumi=18
	$(PYTHON) scripts/insert_glyphs.py $(JP_LIST) $(JP_TTF) Source/fonts/Roobert-20-Medium-table-32-32.png Source/fonts/Roobert-20-Medium.fnt --beta-gumi=21
	$(PYTHON) scripts/insert_glyphs.py $(JP_LIST) $(JP_TTF) Source/fonts/Asheville-Sans-14-Bold-table-20-20.png Source/fonts/Asheville-Sans-14-Bold.fnt --beta-gumi=16
	$(PYTHON) scripts/insert_glyphs.py $(JP_LIST) $(JP_TTF) Source/fonts/Nontendo-Bold-table-10-13.png Source/fonts/Nontendo-Bold.fnt --beta-gumi=12
	touch $@

.PHONY: fonts
fonts: $(FONT_STAMP)

# fonts must be produced before pdc packages Source
all: | fonts db
device: | fonts db
simulator: | fonts db

# Compress recommended settings JSONs and add to pdx
.PHONY: csettings
csettings:
	mkdir -p build/csettings
	for f in src/csettings/*.json; do gzip -c "$$f" > "build/csettings/$$(basename "$$f").gz"; done

# Compress CHANGELOG.md and add to pdx
.PHONY: changelog
changelog:
	gzip -c CHANGELOG.md > build/CHANGELOG.md.gz

# Compress db JSONs to Source/db/ (incremental: only when source newer, clean stale)
.PHONY: db
db:
	@mkdir -p Source/db
	@for f in src/db/*.json; do \
		name=$$(basename "$$f"); \
		target="Source/db/$${name}.gz"; \
		if [ ! -f "$$target" ] || [ "$$f" -nt "$$target" ]; then \
			gzip -c "$$f" > "$$target"; \
		fi; \
	done
	@for f in Source/db/*.json.gz; do \
		name=$$(basename "$$f" .gz); \
		if [ ! -f "src/db/$$name" ]; then \
			rm -f "$$f"; \
		fi; \
	done

# Post-pdc steps: copy csettings to PDX and strip baked JSONs from PDX
define _post_pdc
	-rm -f $(PRODUCT)/version.json $(PRODUCT)/credits.json
	mkdir -p $(PRODUCT)/csettings
	cp build/csettings/*.json.gz $(PRODUCT)/csettings/
	cp build/CHANGELOG.md.gz $(PRODUCT)/
endef

.PHONY: device simulator
device: device_bin csettings changelog
	$(PDC) $(PDCFLAGS) Source $(PRODUCT)
	$(_post_pdc)

simulator: simulator_bin csettings changelog
	$(PDC) $(PDCFLAGS) Source $(PRODUCT)
	$(_post_pdc)

.PHONY: build
build: all csettings changelog
	$(_post_pdc)

.DEFAULT_GOAL := build

# flags for simulator
DYLIB_FLAGS += $(COMMON_FLAGS) $(SIMULATOR_FLAGS)
UDEFS += $(COMMON_FLAGS)

# Generate .clangd config for LSP support
.PHONY: clangd
clangd:
	@echo "Generating .clangd with SDK: $(SDK)"
	@echo 'CompileFlags:' > .clangd
	@echo '  Add: ' >> .clangd
	@echo '    - -I$(SDK)/C_API' >> .clangd
	@$(foreach dir,$(UINCDIR),echo '    - -I$(dir)' >> .clangd;)
	@echo '    - -DTARGET_EXTENSION=1' >> .clangd
	@echo '    - -DTARGET_SIMULATOR=1' >> .clangd
	@echo '    - -include' >> .clangd
	@echo '    - stdint.h' >> .clangd
	@echo '    - -include' >> .clangd
	@echo '    - stdbool.h' >> .clangd
	@echo '    - -include' >> .clangd
	@echo '    - stddef.h' >> .clangd
	@echo '    - -include' >> .clangd
	@echo '    - string.h' >> .clangd
