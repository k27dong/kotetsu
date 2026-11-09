# --- Settings ---

# Set any of these on the command line, e.g. `make CC=clang`.
# CPPFLAGS, CFLAGS and LDFLAGS set there are added to the project's own flags.
PREFIX ?= /usr/local
KOTETSU_FONT_DIR ?= $(CURDIR)/fonts
# dev, release or asan; `make release` and `make asan` set it
MODE := dev

# --- Files ---

SYSTEM := $(shell uname -s)
MODULES := \
  src/config.c \
  src/date.c \
  src/draw/frame.c \
  src/draw/icons.c \
  src/draw/layout.c \
  src/draw/typeset.c \
  src/lunar_table.c \
  src/phrase.c \
  src/record.c \
  src/screen/epd.c \
  src/screen/screen.c \
  src/shared/dashboard.c \
  src/shared/file.c \
  src/shared/http.c \
  src/shared/json.c \
  src/shared/log.c \
  src/shared/text.c \
  src/shared/time.c \
  src/warnings.c \
  src/weather.c
# only Linux has the panel driver (GPIO, SPI) and the Wi-Fi signal reader (nl80211)
ifeq ($(SYSTEM),Linux)
  MODULES += src/screen/board_linux.c
else
  MODULES += src/screen/board_none.c
endif
MAIN := src/main.c
TESTS := $(sort $(wildcard tests/test_*.c))
FORMAT_FILES := $(wildcard src/*.[ch] src/*/*.[ch] tests/*.[ch])

BUILD := build/$(MODE)
OBJECTS := $(MODULES:src/%.c=$(BUILD)/%.o)
LIBRARY := $(BUILD)/libkotetsu_core.a
PROGRAM := $(BUILD)/kotetsu
TEST_OBJECTS := $(TESTS:tests/%.c=$(BUILD)/tests/%.o)
TEST_PROGRAMS := $(TESTS:tests/%.c=$(BUILD)/tests/%)

# --- Libraries ---

PACKAGES := pangocairo pangoft2 fontconfig glib-2.0 cairo libcurl libcjson
# for the panel driver and the Wi-Fi signal reader
ifeq ($(SYSTEM),Linux)
  PACKAGES += libgpiod libnl-genl-3.0
endif
ifeq ($(SYSTEM),Darwin)
  # LLVM's clang-tidy finds the macOS SDK only through SDKROOT
  ifndef SDKROOT
    SDKROOT := $(shell xcrun --show-sdk-path)
  endif
  export SDKROOT
  # Homebrew's curl is keg-only, so without this pkg-config finds macOS's older libcurl
  PKG_CONFIG_PATH := $(shell brew --prefix curl)/lib/pkgconfig$(if $(PKG_CONFIG_PATH),:$(PKG_CONFIG_PATH))
endif
# Make 3.81's $(shell) sees only the environment make started with, so the path is passed in
PKG_CONFIG := PKG_CONFIG_PATH='$(PKG_CONFIG_PATH)' pkg-config
PACKAGE_ERRORS := $(shell $(PKG_CONFIG) --print-errors --exists $(PACKAGES) 2>&1)
ifneq ($(PACKAGE_ERRORS),)
  $(error $(PACKAGE_ERRORS))
endif
# -isystem instead of -I, so warnings inside the libraries' headers can't fail the build
PACKAGE_CFLAGS := $(patsubst -I%,-isystem %,$(shell $(PKG_CONFIG) --cflags $(PACKAGES)))
LDLIBS := $(shell $(PKG_CONFIG) --libs $(PACKAGES))

# --- Compile flags ---

# glibc hides POSIX functions under -std=c23 unless _DEFAULT_SOURCE is defined
PROJECT_CFLAGS := -std=c23 -D_DEFAULT_SOURCE -pthread -Isrc $(PACKAGE_CFLAGS) \
  -finput-charset=UTF-8 -fexec-charset=UTF-8 \
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wvla \
  -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wimplicit-fallthrough
ifeq ($(MODE),dev)
  MODE_CFLAGS := -Werror
else ifeq ($(MODE),release)
  MODE_CFLAGS := -O2 -fstrict-flex-arrays=3
  # Apple clang has no -fhardened, and Apple's linker rejects the GNU ld flags
  ifeq ($(SYSTEM),Linux)
    MODE_CFLAGS += -fhardened -mbranch-protection=standard
    MODE_LDFLAGS := -Wl,-z,noexecstack -Wl,--as-needed
  endif
else ifeq ($(MODE),asan)
  MODE_CFLAGS := -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
else
  $(error MODE is dev, release or asan, not "$(MODE)")
endif
ALL_CFLAGS := $(PROJECT_CFLAGS) $(MODE_CFLAGS) $(CPPFLAGS) $(CFLAGS)
ALL_LDFLAGS := $(MODE_LDFLAGS) $(LDFLAGS)
TEST_CFLAGS := -isystem tests/unity

# --- Commands ---

.PHONY: all release asan test lint format editor install clean help

all: $(PROGRAM)

release:
	@$(MAKE) --no-print-directory MODE=release

asan:
	@$(MAKE) --no-print-directory MODE=asan

test: $(PROGRAM) $(TEST_PROGRAMS)
	@KOTETSU_FONT_DIR='$(KOTETSU_FONT_DIR)' tests/run.sh $(TEST_PROGRAMS)

lint:
	clang-format --dry-run --Werror $(FORMAT_FILES)
	clang-tidy --quiet --warnings-as-errors='*' $(MODULES) $(MAIN) -- $(ALL_CFLAGS)

format:
	clang-format -i $(FORMAT_FILES)

# one JSON entry per file, inside [ ] and without a comma after the last
editor:
	@for file in $(MODULES) $(MAIN) $(TESTS); do \
	  printf '{"directory": "%s", "file": "%s", "command": "%s -c %s"},\n' \
	    '$(CURDIR)' "$$file" '$(CC) $(ALL_CFLAGS) $(TEST_CFLAGS)' "$$file"; \
	done | sed -e '1s/^/[/' -e '$$s/,$$/]/' > compile_commands.json

install: release
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/release/kotetsu $(DESTDIR)$(PREFIX)/bin/kotetsu

clean:
	rm -rf build

help:
	@echo 'make          build for development, with warnings as errors, into build/dev/'
	@echo 'make release  build optimized and hardened into build/release/'
	@echo 'make asan     build with AddressSanitizer and UBSan into build/asan/'
	@echo 'make test     build and run the unit tests; needs the fonts in fonts/'
	@echo 'make lint     check the formatting and run clang-tidy; changes no files'
	@echo 'make format   format src/ and tests/ in place'
	@echo 'make editor   write compile_commands.json for clangd editors such as Zed'
	@echo 'make install  install the release build to $(PREFIX)/bin'
	@echo 'make clean    delete build/'
	@echo 'make help     show this list'

# --- Build rules ---

# a failed command deletes the file it was writing
.DELETE_ON_ERROR:

# each rule prints one short line; `make -n` lists the full commands
$(PROGRAM): $(BUILD)/main.o $(LIBRARY)
	@echo "  LINK  $@"
	@$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $@ $^ $(LDLIBS)

$(LIBRARY): $(OBJECTS)
	@echo "  AR    $@"
	@rm -f $@ && $(AR) rcs $@ $^

$(BUILD)/%.o: src/%.c
	@mkdir -p $(@D)
	@echo "  CC    $<"
	@$(CC) $(ALL_CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/tests/%.o: tests/%.c
	@mkdir -p $(@D)
	@echo "  CC    $<"
	@$(CC) $(ALL_CFLAGS) $(TEST_CFLAGS) -MMD -MP -c -o $@ $<

# Unity is third-party code, so -w keeps its warnings out of the build
$(BUILD)/tests/unity.o: tests/unity/unity.c
	@mkdir -p $(@D)
	@echo "  CC    $<"
	@$(CC) $(ALL_CFLAGS) $(TEST_CFLAGS) -w -c -o $@ $<

$(TEST_PROGRAMS): $(BUILD)/tests/%: $(BUILD)/tests/%.o $(BUILD)/tests/unity.o $(LIBRARY)
	@echo "  LINK  $@"
	@$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $@ $^ $(LDLIBS)

include $(wildcard $(OBJECTS:.o=.d) $(BUILD)/main.d $(TEST_OBJECTS:.o=.d))
