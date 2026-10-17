# Prince of Persia (SDLPoP) — headless Linux harness
#
#   make            → build/pop_host
#   make SAN=1      → with AddressSanitizer + UBSan
#
#   ./build/pop_host "Prince of Persia.pop" -t 20000 -e 1000 -o /tmp/pop
#
# Runs the same SDL shim, heap and asset-pack code as the G&W core with a
# virtual clock, scripted keys, and PPM/WAV dumps (see pop/pop_host.c).

BUILD_DIR = build
TARGET = $(BUILD_DIR)/pop_host

POP_SRC = ../src/sdlpop
POP_PORT = ../src/port

SDLPOP_SOURCES = \
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

PORT_SOURCES = \
	$(POP_PORT)/pop_sdl.c \
	$(POP_PORT)/pop_alloc.c \
	$(POP_PORT)/pop_pack.c \
	$(POP_PORT)/gnw_music.c \
	$(POP_PORT)/pop_file.c \
	pop_host.c

SAN ?= 0
OPT ?= -O2 -g
ifeq ($(SAN),1)
OPT += -fsanitize=address,undefined -fno-omit-frame-pointer -DPOP_HOST_SYSTEM_MALLOC
LDFLAGS += -fsanitize=address,undefined
endif

INCLUDES = -I$(POP_PORT)/sdl -I$(POP_PORT) -I$(POP_SRC)
CFLAGS = $(OPT) -std=gnu11 -DGNW -Wall -Wno-unused-variable -Wno-unused-function \
	-Wno-unused-but-set-variable -Wno-format-truncation -Wno-stringop-truncation \
	-Wno-misleading-indentation -Wno-maybe-uninitialized $(INCLUDES)
SDLPOP_CFLAGS = $(CFLAGS) -include $(POP_PORT)/gnw_pop_glue.h

SDLPOP_OBJECTS = $(addprefix $(BUILD_DIR)/sdlpop_,$(notdir $(SDLPOP_SOURCES:.c=.o)))
PORT_OBJECTS = $(addprefix $(BUILD_DIR)/port_,$(notdir $(PORT_SOURCES:.c=.o)))

vpath %.c $(sort $(dir $(SDLPOP_SOURCES) $(PORT_SOURCES)))

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/sdlpop_%.o: %.c | $(BUILD_DIR)
	$(CC) -c $(SDLPOP_CFLAGS) $< -o $@

$(BUILD_DIR)/port_%.o: %.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(TARGET): $(SDLPOP_OBJECTS) $(PORT_OBJECTS)
	$(CC) $^ $(LDFLAGS) -lm -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
