# Prince of Persia (SDLPoP) for Game & Watch Retro-Go SD — GWHB homebrew.
#
#   make                  — build + pack → PrinceOfPersia.bin
#   make docker           — same build inside the Retro-Go SD builder image
#   make harness          — headless Linux test harness (harness/README.md)
#
# SD card (Retro-Go SD v2.0.0 or later):
#   /homebrews/PrinceOfPersia.bin   (this homebrew)
#   /homebrews/PrinceOfPersia.pop   (game data: tools/pop_pack.py, same name)
#
# Verbose compiler lines: make V=

#######################################
# Project identity
#######################################
PROJECT_KIND := homebrew

CORE_NAME  := pop
CORE_ENTRY := app_main

HB_NAME    := Prince of Persia
PACKED_BIN := PrinceOfPersia.bin

POP_SRC  := src/sdlpop
POP_PORT := src/port

# SDLPoP 1.24 (the original DOS game engine, C reimplementation)
SDLPOP_C_SOURCES := \
$(POP_SRC)/data.c \
$(POP_SRC)/seg000.c \
$(POP_SRC)/seg001.c \
$(POP_SRC)/seg002.c \
$(POP_SRC)/seg003.c \
$(POP_SRC)/seg004.c \
$(POP_SRC)/seg005.c \
$(POP_SRC)/seg006.c \
$(POP_SRC)/seg007.c \
$(POP_SRC)/seg008.c \
$(POP_SRC)/seg009.c \
$(POP_SRC)/seqtbl.c \
$(POP_SRC)/options.c \
$(POP_SRC)/midi.c \
$(POP_SRC)/opl3.c \
$(POP_PORT)/pop_core.c

# Game & Watch side: SDL shim, heap, asset pack, retro-go glue
PORT_C_SOURCES := \
$(POP_PORT)/pop_sdl.c \
$(POP_PORT)/pop_alloc.c \
$(POP_PORT)/pop_pack.c \
$(POP_PORT)/gnw_music.c \
$(POP_PORT)/pop_file.c \
$(POP_PORT)/pop_libc.c \
$(POP_PORT)/main_pop.c

CORE_C_SOURCES := $(SDLPOP_C_SOURCES) $(PORT_C_SOURCES)

CORE_C_INCLUDES := -I$(POP_PORT)/sdl -I$(POP_PORT)

CORE_C_DEFS := -DPROJECT_KIND_HOMEBREW=1 -DPOP_DEVICE=1

# The game is CPU bound (OPL3 music synthesis, blits): optimize for speed.
OPT := -O2

GNW_CORE_SDK ?= sdk
BUILD_DIR ?= build/$(PROJECT_KIND)

include $(GNW_CORE_SDK)/Makefile

#######################################
# SDLPoP specific flags
#######################################
# SDLPoP sources get -DGNW and the glue header that redirects malloc/free/
# fopen to the game heap and to the asset pack.
SDLPOP_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(SDLPOP_C_SOURCES:.c=.o)))
SDLPOP_FLAGS := -DGNW -include $(POP_PORT)/gnw_pop_glue.h -std=gnu11 \
	-Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable \
	-Wno-format-truncation -Wno-stringop-truncation -Wno-misleading-indentation \
	-Wno-maybe-uninitialized -Wno-address -Wno-format -Wno-char-subscripts \
	-Wno-dangling-pointer
$(SDLPOP_OBJECTS): CFLAGS += $(SDLPOP_FLAGS)
$(C_OBJECTS): Makefile

#######################################
# Packed header version
#######################################
CORE_VERSION ?= $(shell git describe --tags --dirty 2>/dev/null || echo NOTAG)

#######################################
# Pack (GWHB)
#######################################
PACK_HOMEBREW := $(GNW_CORE_SDK)/tools/pack_homebrew.py
MAKE_COVER    := scripts/make_cover.py
COVER_SRC     := src/assets/cover.png
COVER_JPG     := $(BUILD_DIR)/cover.jpg

.PHONY: pack cover
cover: $(COVER_JPG)

# The cover art (src/assets/cover.png, 128x96: the game's box and a room).
# Must stay <= 186x100 and <= 10 KiB (firmware cover cache).
$(COVER_JPG): $(MAKE_COVER) $(COVER_SRC) | $(BUILD_DIR)
	$(V)$(ECHO) "[ COVER ] $(COVER_JPG)"
	$(V)python3 $(MAKE_COVER) --in $(COVER_SRC) --out $(COVER_JPG)

pack: $(TARGET_BIN) $(COVER_JPG)
	$(V)$(ECHO) [ PACK GWHB ] $(PACKED_BIN) version=$(CORE_VERSION)
	$(V)python3 $(PACK_HOMEBREW) \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--name "$(HB_NAME)" --version "$(CORE_VERSION)" \
		--cover $(COVER_JPG) \
		--out $(PACKED_BIN)

all: pack

clean::
	$(V)rm -f $(PACKED_BIN) $(COVER_JPG)

# Read-only helpers for CI / scripts (make print-PACKED_BIN, etc.).
.PHONY: print-PROJECT_KIND print-PACKED_BIN print-CORE_NAME print-DOCKER_IMAGE \
	print-TARGET_ELF print-TARGET_MAP print-CORE_VERSION
print-PROJECT_KIND:
	@echo $(PROJECT_KIND)
print-PACKED_BIN:
	@echo $(PACKED_BIN)
print-CORE_NAME:
	@echo $(CORE_NAME)
print-DOCKER_IMAGE:
	@echo $(DOCKER_IMAGE)
print-TARGET_ELF:
	@echo $(TARGET_ELF)
print-TARGET_MAP:
	@echo $(BUILD_DIR)/$(CORE_NAME)_core.map
print-CORE_VERSION:
	@echo $(CORE_VERSION)

#######################################
# Headless Linux harness (same game + port code, virtual clock)
#######################################
.PHONY: harness
harness:
	$(MAKE) -C harness

#######################################
# Docker (same image as the firmware repo)
#######################################
.PHONY: docker docker_pull docker_shell

RELEASE_VERSION ?= v1.5
DOCKER_REPOSITORY ?= sylverb/retro-go-sd-builder
DOCKER_IMAGE ?= $(DOCKER_REPOSITORY):$(RELEASE_VERSION)

DOCKER_TTY_FLAG := $(shell if [ -t 0 ]; then echo -it; else echo; fi)
DOCKER_USER := $(shell id -u):$(shell id -g)
DOCKER_RUN := docker run --rm $(DOCKER_TTY_FLAG) \
	--user $(DOCKER_USER) \
	-v "$(CURDIR):/opt/workdir" \
	-w /opt/workdir \
	$(DOCKER_IMAGE)

docker:
	$(V)$(ECHO) "[ DOCKER ]" $(DOCKER_IMAGE)
	$(V)$(DOCKER_RUN) make --no-print-directory -j$$(nproc)

docker_pull:
	$(V)docker pull $(DOCKER_IMAGE)

docker_shell:
	$(DOCKER_RUN) bash
