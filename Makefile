












CC        ?= cc
CSTD      ?= -std=c11
OPT       ?= -O2
WARN      := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
             -Wconversion -Wno-sign-conversion
CPPFLAGS  ?=
CFLAGS    ?=
LDFLAGS   ?=
BUILD     ?= build



VERSION   := 2.0.0
PREFIX    ?= /usr/local
BINDIR    ?= $(PREFIX)/bin
LIBDIR    ?= $(PREFIX)/lib
DATADIR   ?= $(PREFIX)/share/libregpu
DOCDIR    ?= $(PREFIX)/share/doc/libregpu
DESTDIR   ?=
INSTALL   ?= install



CPPFLAGS  += -DLIBREGPU_ASSET_DIR='"$(DATADIR)/assets"' -I. -Iassets -Isrc

.DEFAULT_GOAL := libregpu


DIST_UI   := LibreGPU-ui-$(VERSION).zip
DIST_DIR  := dist/LibreGPU-rc2
CORE_DIST_FILES :=  src/lib.c src/run.c src/ui.c src/main.c
UI_ASSET_FILES  := assets/logo.svg assets/add.svg assets/close.svg
UI_DIST_FILES   :=  Makefile nuklear.h asan.supp assets/libregpu_ui_icons.h \
                   src/sim.c \
                   src/gui.c src/lib.c src/run.c src/ui.c src/main.c
DOC_FILES := 

CORE_SRCS := src/main.c src/run.c src/ui.c src/lib.c
UI_SRCS   := src/gui.c \
             src/lib.c src/run.c src/ui.c




GPU_SRCS  := src/sim.c src/run.c src/ui.c src/lib.c
INTERPOSE_SRCS := src/sim.c
GL_LIBS   := -lEGL -lGL

NK_DEFS   := -DNK_INCLUDE_VERTEX_BUFFER_OUTPUT \
             -DNK_INCLUDE_FONT_BAKING \
             -DNK_INCLUDE_DEFAULT_FONT \
             -DNK_UINT_DRAW_INDEX

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)

CORE_OBJS := $(patsubst %.c,$(BUILD)/core/%.o,$(CORE_SRCS))
UI_OBJS   := $(patsubst %.c,$(BUILD)/ui/%.o,$(UI_SRCS))
GPU_OBJS  := $(patsubst %.c,$(BUILD)/gpu/%.o,$(GPU_SRCS))

.PHONY: cli ui test check asan gpu gpu-check interpose gpu-asan clean \
        ui-check install uninstall dist

cli: libregpu-core

ifeq ($(strip $(SDL_LIBS)),)
ui:
	@echo "libregpu needs SDL2 development files." >&2
	@echo "  Debian/Ubuntu: sudo apt install libsdl2-dev" >&2
	@echo "  Fedora:        sudo dnf install SDL2-devel" >&2
	@echo "  FreeBSD:       sudo pkg install sdl2" >&2
	@echo "  macOS:         brew install sdl2" >&2
	@echo "  Windows:       use MSYS2/mingw-w64 or vcpkg" >&2
	@exit 1
else
ui: libregpu
endif

libregpu-core: $(CORE_OBJS)
	$(CC) $(LDFLAGS) $^ -o libregpu-core -lm

libregpu: $(UI_OBJS)
	$(CC) $(LDFLAGS) $^ -o libregpu $(SDL_LIBS) -lm


gpu: libregpu-gpu

libregpu-gpu: $(GPU_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(SDL_LIBS) $(GL_LIBS) -lm


gpu-check: libregpu-gpu
	./libregpu-gpu --frames 32 --quiet


interpose: liblibregpu_interpose.so

liblibregpu_interpose.so: $(INTERPOSE_SRCS)
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) $(CFLAGS) -DSIM_INTERPOSE -fPIC -shared \
		$(INTERPOSE_SRCS) -o $@ -ldl

$(BUILD)/gpu/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) $(CFLAGS) -DSIM_GPU \
		$(SDL_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/core/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/ui/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) $(CFLAGS) \
		$(NK_DEFS) $(SDL_CFLAGS) -MMD -MP -c $< -o $@




$(BUILD)/ui/gui.o: WARN := $(filter-out -Wconversion,$(WARN))

test: libregpu-core
	./libregpu-core --selftest

check: libregpu-core
	./libregpu-core --selftest



ui-check: ui
	./libregpu --selftest

install: libregpu-core libregpu liblibregpu_interpose.so
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(LIBDIR)/libregpu" \
		"$(DESTDIR)$(DOCDIR)" "$(DESTDIR)$(DATADIR)/assets"
	$(INSTALL) -m 0755 libregpu-core libregpu "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 0644 liblibregpu_interpose.so "$(DESTDIR)$(LIBDIR)/libregpu"
	$(INSTALL) -m 0644 $(UI_ASSET_FILES) "$(DESTDIR)$(DATADIR)/assets"
	@echo "installed LIBREGPU $(VERSION) under $(DESTDIR)$(PREFIX)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/libregpu-core" "$(DESTDIR)$(BINDIR)/libregpu"
	rm -f "$(DESTDIR)$(LIBDIR)/libregpu/liblibregpu_interpose.so"
	rm -f $(addprefix "$(DESTDIR)$(DOCDIR)/",$(notdir $(DOC_FILES)))
	rm -f $(addprefix "$(DESTDIR)$(DATADIR)/assets/",$(notdir $(UI_ASSET_FILES)))
	-rmdir "$(DESTDIR)$(LIBDIR)/libregpu" "$(DESTDIR)$(DATADIR)/assets" \
		"$(DESTDIR)$(DATADIR)" "$(DESTDIR)$(DOCDIR)" 2>/dev/null || true
	@echo "removed LIBREGPU $(VERSION) from $(DESTDIR)$(PREFIX)"



dist:
	rm -rf dist $(DIST_UI)
	mkdir -p dist/LibreGPU-rc2
	cp Makefile nuklear.h dist/LibreGPU-rc2/
	mkdir -p dist/LibreGPU-rc2/src dist/LibreGPU-rc2/assets
	cp src/*.c dist/LibreGPU-rc2/src/
	cp assets/* dist/LibreGPU-rc2/assets/
	cp asan.supp dist/LibreGPU-rc2/
	(cd dist && zip -qr "../$(DIST_UI)" LibreGPU-rc2)
	rm -rf dist
	@echo "wrote $(DIST_UI)"

asan:
	$(CC) $(CSTD) $(WARN) -O1 -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer $(CPPFLAGS) $(CORE_SRCS) -o libregpu-core-asan -lm
	$(CC) $(CSTD) $(WARN) -O1 -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer $(CPPFLAGS) $(NK_DEFS) $(SDL_CFLAGS) \
		$(UI_SRCS) -o libregpu-asan $(SDL_LIBS) -lm
	ASAN_OPTIONS=detect_leaks=1 ./libregpu-core-asan --selftest
	ASAN_OPTIONS=detect_leaks=1 ./libregpu-asan --selftest




gpu-asan:
	$(CC) $(CSTD) $(WARN) -O1 -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer $(CPPFLAGS) $(SDL_CFLAGS) -DSIM_GPU \
		$(GPU_SRCS) -o libregpu-gpu-asan $(SDL_LIBS) $(GL_LIBS) -lm
	ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=suppressions=markdown/asan.supp \
		./libregpu-gpu-asan --frames 16 --quiet

clean:
	rm -rf $(BUILD) build-asan dist libregpu-core libregpu libregpu-core-asan libregpu-asan \
		libregpu-gpu libregpu-gpu-asan liblibregpu_interpose.so

-include $(CORE_OBJS:.o=.d) $(UI_OBJS:.o=.d) $(GPU_OBJS:.o=.d)
