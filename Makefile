CC        ?= cc
CSTD      ?= -std=c11
OPT       ?= -O2
WARN      := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
             -Wconversion -Wno-sign-conversion
CPPFLAGS  ?=
CFLAGS    ?=
LDFLAGS   ?=
BUILD     ?= build

SRC_DIR   := src
SRCS      := $(SRC_DIR)/main.c $(SRC_DIR)/run.c $(SRC_DIR)/ui.c \
             $(SRC_DIR)/lib.c $(SRC_DIR)/gui.c
CORE_SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/run.c $(SRC_DIR)/ui.c $(SRC_DIR)/lib.c
OBJS      := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRCS))

NK_DEFS   := -DNK_INCLUDE_VERTEX_BUFFER_OUTPUT \
             -DNK_INCLUDE_FONT_BAKING \
             -DNK_INCLUDE_DEFAULT_FONT \
             -DNK_UINT_DRAW_INDEX

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)

INCLUDES  := -I.

.PHONY: all test check asan analyze demo bench clean

ifeq ($(strip $(SDL_LIBS)),)
all:
	@echo "libregpu needs SDL2 development files." >&2
	@echo "  Debian/Ubuntu: sudo apt install libsdl2-dev" >&2
	@echo "  Fedora:        sudo dnf install SDL2-devel" >&2
	@echo "  FreeBSD:       sudo pkg install sdl2" >&2
	@echo "  macOS:         brew install sdl2" >&2
	@echo "  Windows:       use MSYS2/mingw-w64 or vcpkg" >&2
	@exit 1
else
all: libregpu
endif

libregpu: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(SDL_LIBS) -lm

$(BUILD)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) \
		$(NK_DEFS) $(SDL_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/gui.o: WARN := $(filter-out -Wconversion,$(WARN)) -Wno-implicit-fallthrough

test: libregpu
	./libregpu --selftest

check: test asan analyze

analyze:
	@set -e; for f in $(CORE_SRCS); do \
	  echo "== $$f"; \
	  $(CC) $(CSTD) $(WARN) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -fanalyzer -Werror -c $$f -o /dev/null; \
	done; echo "analysis finished"

demo: libregpu
	./libregpu --demo --frames 60

bench: libregpu
	./libregpu --demo --frames 120 --quiet --baseline-sample 5

asan:
	@mkdir -p $(BUILD)
	$(CC) $(CSTD) $(filter-out -Wconversion,$(WARN)) -Wno-implicit-fallthrough -O1 -g \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CPPFLAGS) $(CFLAGS) $(INCLUDES) $(NK_DEFS) $(SDL_CFLAGS) \
		$(SRCS) -o $(BUILD)/libregpu-asan $(SDL_LIBS) -lm
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD)/libregpu-asan --selftest

clean:
	rm -rf $(BUILD) libregpu libregpu-ui libregpu-asan libregpu-ui-asan

-include $(OBJS:.o=.d)
