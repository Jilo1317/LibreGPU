
































.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.SUFFIXES:





CC       ?= cc
STD      ?= -std=c11
OPT      ?= -O2
CFLAGS   ?= $(OPT) -g
WARN     := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
            -Wconversion -Wno-sign-conversion
WERROR   ?=
CPPFLAGS += -Isrc
LDFLAGS  ?=
LDLIBS   += -lm




AUTOINIT := $(shell printf 'int main(void){return 0;}\n' | \
              $(CC) -x c - -c -o /dev/null -ftrivial-auto-var-init=pattern \
              2>/dev/null && echo -ftrivial-auto-var-init=pattern)


HAVE_ANALYZER := $(shell printf 'int main(void){return 0;}\n' | \
                    $(CC) -x c - -c -o /dev/null -fanalyzer 2>/dev/null \
                    && echo yes)





SRC_DIR   := src
BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
ASAN_DIR  := $(BUILD_DIR)/asan
STRICT_DIR:= $(BUILD_DIR)/strict

SRC_NAMES := main.c run.c ui.c lib.c
SRCS      := $(addprefix $(SRC_DIR)/,$(SRC_NAMES))
OBJS      := $(SRC_NAMES:%.c=$(OBJ_DIR)/%.o)
ASAN_OBJS := $(SRC_NAMES:%.c=$(ASAN_DIR)/%.o)
DEPS      := $(OBJS:.o=.d) $(ASAN_OBJS:.o=.d)

BIN       := libregpu
ASAN_BIN  := libregpu-asan

ASAN_FLAGS := -O1 -g -fsanitize=address,undefined \
              -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
              $(AUTOINIT)





all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(STD) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(STD) $(WARN) $(WERROR) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(ASAN_BIN): $(ASAN_OBJS)
	$(CC) $(STD) $(ASAN_FLAGS) $(LDFLAGS) -o $@ $(ASAN_OBJS) $(LDLIBS)

$(ASAN_DIR)/%.o: $(SRC_DIR)/%.c | $(ASAN_DIR)
	$(CC) $(STD) $(WARN) $(WERROR) $(ASAN_FLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR) $(ASAN_DIR):
	mkdir -p $@


strict:
	@$(MAKE) --no-print-directory \
	  OBJ_DIR=$(STRICT_DIR)/obj BIN=$(STRICT_DIR)/$(BIN) WERROR=-Werror all





test: $(BIN)
	./$(BIN) --selftest

asan: $(ASAN_BIN)
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./$(ASAN_BIN) --selftest

analyze:
	@if [ "$(HAVE_ANALYZER)" != "yes" ]; then \
	  echo "gcc -fanalyzer not supported by $(CC); skipped"; exit 0; \
	fi; \
	set -e; for f in $(SRCS); do \
	  echo "== $$f"; \
	  $(CC) $(STD) $(WARN) $(CPPFLAGS) -fanalyzer -Werror -c $$f -o /dev/null; \
	done; echo "analysis finished"

check: strict test asan analyze





demo: $(BIN)
	./$(BIN) --demo --frames 60

bench: $(BIN)
	./$(BIN) --demo --frames 120 --quiet --baseline-sample 5

clean:
	rm -rf $(BUILD_DIR) $(BIN) $(ASAN_BIN)

help:
	@printf '%s\n' \
	  'LIBREGPU targets:' \
	  '  make                 build ./libregpu' \
	  '  make test            run the self-test' \
	  '  make strict          rebuild with -Werror (build/strict)' \
	  '  make asan            ASan + UBSan + LeakSanitizer self-test' \
	  '  make analyze         gcc -fanalyzer on every source file' \
	  '  make check           strict + test + asan + analyze' \
	  '  make demo            60-frame demo' \
	  '  make bench           120 frames with baseline samples' \
	  '  make clean           remove build/ and both binaries' \
	  '  make help            this summary' \
	  '' \
	  'Overrides: CC=... OPT=... CFLAGS=... WERROR=-Werror LDFLAGS=... LDLIBS=...'

.PHONY: all strict test asan analyze check demo bench clean help

-include $(DEPS)
