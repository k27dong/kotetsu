# kotetsu: the program on the Pi that draws the wall screen.
# `make help` lists the commands.

PREFIX = /usr/local
KOTETSU_FONT_DIR = $(CURDIR)/fonts
VERSION := $(shell git describe --always --dirty 2>/dev/null || echo unknown)

# The program's files. The panel driver needs Linux; elsewhere a stand-in says there is no panel.
SRC = src/config.c src/date.c src/lunar_table.c src/phrase.c src/record.c src/state.c \
      src/warnings.c src/weather.c src/wifi.c \
      src/draw/frame.c src/draw/icons.c src/draw/layout.c src/draw/typeset.c \
      src/screen/epd.c src/screen/screen.c \
      src/shared/dashboard.c src/shared/file.c src/shared/http.c src/shared/json.c \
      src/shared/log.c src/shared/text.c src/shared/time.c
TESTS = $(wildcard tests/test_*.c)
LIBS = pangocairo pangoft2 fontconfig glib-2.0 cairo libcurl libcjson

ifeq ($(shell uname),Linux)
  SRC += src/screen/board_linux.c
  LIBS += libgpiod libnl-genl-3.0
  HARDENING = -fhardened -mbranch-protection=standard
  LINK_HARDENING = -Wl,-z,noexecstack -Wl,--as-needed
else
  SRC += src/screen/board_none.c
  # clang-tidy finds the macOS headers only through SDKROOT
  export SDKROOT := $(shell xcrun --show-sdk-path)
endif

# The same flags on the Mac and on the Pi; add your own with `make CFLAGS=...`.
WARNINGS = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wvla \
           -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wimplicit-fallthrough -Werror
# Library headers come in with -isystem, so warnings inside them can't stop the build.
LIB_CFLAGS := $(patsubst -I%,-isystem %,$(shell pkg-config --cflags $(LIBS)))
ALL_CFLAGS = -std=c23 -D_DEFAULT_SOURCE -pthread -O2 -g -Isrc $(WARNINGS) $(HARDENING) \
             -DKOTETSU_VERSION='"$(VERSION)"' $(LIB_CFLAGS) $(CFLAGS)
LDLIBS := $(shell pkg-config --libs $(LIBS))

OBJ = $(SRC:src/%.c=build/%.o)
TEST_PROGRAMS = $(TESTS:tests/%.c=build/%)

all: build/kotetsu

build/kotetsu: build/main.o $(OBJ)
	$(CC) $(ALL_CFLAGS) $(LINK_HARDENING) -o $@ build/main.o $(OBJ) $(LDLIBS)

build/%.o: src/%.c
	mkdir -p $(@D)
	$(CC) $(ALL_CFLAGS) -MMD -MP -c -o $@ $<

build/tests/%.o: tests/%.c
	mkdir -p $(@D)
	$(CC) $(ALL_CFLAGS) -isystem tests/unity -MMD -MP -c -o $@ $<

# Each test is its own program, linked with every file except main.c.
build/test_%: build/tests/test_%.o $(OBJ) build/unity.o
	$(CC) $(ALL_CFLAGS) $(LINK_HARDENING) -o $@ $^ $(LDLIBS)

# Unity is someone else's code, so its warnings are switched off.
build/unity.o: tests/unity/unity.c
	mkdir -p build
	$(CC) $(ALL_CFLAGS) -w -c -o $@ $<

test: $(TEST_PROGRAMS)
	cd tests && for test in $(TEST_PROGRAMS); do \
	  TZ=Europe/Warsaw KOTETSU_FONT_DIR=$(KOTETSU_FONT_DIR) ../$$test || exit 1; \
	done

lint:
	clang-format --dry-run --Werror src/*.[ch] src/*/*.[ch] tests/*.[ch]
	clang-tidy --quiet --warnings-as-errors='*' $(SRC) src/main.c -- $(ALL_CFLAGS)

format:
	clang-format -i src/*.[ch] src/*/*.[ch] tests/*.[ch]

# clangd reads the compiler flags from compile_flags.txt, one per line.
editor:
	echo $(ALL_CFLAGS) -isystem tests/unity | tr ' ' '\n' > compile_flags.txt

install: build/kotetsu
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/kotetsu $(DESTDIR)$(PREFIX)/bin/kotetsu

clean:
	rm -rf build compile_flags.txt

help:
	@echo 'make          build build/kotetsu'
	@echo 'make test     build and run the unit tests (needs the fonts in fonts/)'
	@echo 'make lint     check formatting and run clang-tidy'
	@echo 'make format   format the code in place'
	@echo 'make editor   write compile_flags.txt for clangd'
	@echo 'make install  install build/kotetsu to $(PREFIX)/bin'
	@echo 'make clean    delete build/'
	@echo 'with AddressSanitizer: make clean && make test CFLAGS="-O1 -fsanitize=address,undefined -fno-sanitize-recover=all"'

.PHONY: all test lint format editor install clean help

# A command that fails deletes the half-written file it leaves behind.
.DELETE_ON_ERROR:

# Test objects stay after make test, so the next run compiles nothing.
.SECONDARY:

-include $(OBJ:.o=.d) build/main.d $(TESTS:tests/%.c=build/tests/%.d)
