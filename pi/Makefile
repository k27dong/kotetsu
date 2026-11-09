# Settings: set any of these on the command line, e.g. `make CC=clang CFLAGS=-g`; CFLAGS and LDFLAGS add to every build
PREFIX ?= /usr/local
KOTETSU_FONT_DIR ?= $(CURDIR)/fonts

# Sources: only Linux has the panel driver (GPIO, SPI) and the Wi-Fi signal reader (nl80211)
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
PACKAGES := pangocairo pangoft2 fontconfig glib-2.0 cairo libcurl libcjson
SYSTEM := $(shell uname -s)
ifeq ($(SYSTEM),Linux)
MODULES += src/screen/board_linux.c
PACKAGES += libgpiod libnl-genl-3.0
else
MODULES += src/screen/board_none.c
endif
TESTS := $(sort $(wildcard tests/test_*.c))
TEST_PROGRAMS := $(TESTS:tests/%.c=build/tests/%)
FORMAT_FILES := $(sort $(shell find src tests -name '*.[ch]' -not -path 'tests/unity/*'))

# Dependencies: pkg-config finds each library, and a missing one stops make with its name
ifeq ($(SYSTEM),Darwin)
ifndef SDKROOT
SDKROOT := $(shell xcrun --show-sdk-path)
endif
export SDKROOT
# Homebrew's curl is keg-only, so without this pkg-config finds macOS's older libcurl
PKG_CONFIG_PATH := $(shell brew --prefix curl)/lib/pkgconfig$(if $(PKG_CONFIG_PATH),:$(PKG_CONFIG_PATH))
endif
# Make 3.81's $(shell) sees only the starting environment, so the path is passed in
PKG_CONFIG := PKG_CONFIG_PATH='$(PKG_CONFIG_PATH)' pkg-config
ifneq ($(shell $(PKG_CONFIG) --exists $(PACKAGES) && echo found),found)
$(error pkg-config can't find: $(strip $(foreach package,$(PACKAGES),$(if $(shell $(PKG_CONFIG) --exists $(package) && echo found),,$(package)))))
endif
PACKAGE_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PACKAGES))
LDLIBS := $(shell $(PKG_CONFIG) --libs $(PACKAGES))

# Compile flags: every build uses the shared ones, and its build directory adds its own
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wvla \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wimplicit-fallthrough
SHARED_CFLAGS := -std=c23 $(WARNINGS) -finput-charset=UTF-8 -fexec-charset=UTF-8 -pthread -Isrc
SHARED_CFLAGS += -D_DEFAULT_SOURCE # glibc hides POSIX functions under -std=c23 without it
SHARED_CFLAGS += $(patsubst -I%,-isystem %,$(PACKAGE_CFLAGS)) # so the libraries' header warnings can't fail the build
DEV_CFLAGS := -Werror
TEST_CFLAGS := $(DEV_CFLAGS) -isystem tests/unity
RELEASE_CFLAGS := -O2 -fstrict-flex-arrays=3
ASAN_CFLAGS := -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
# Linux only: Apple clang has no -fhardened, and Apple's linker rejects the GNU ld flags
ifeq ($(SYSTEM),Linux)
RELEASE_CFLAGS += -fhardened -mbranch-protection=standard
RELEASE_LDFLAGS := -Wl,-z,noexecstack -Wl,--as-needed
endif
build/%: BUILD_CFLAGS := $(DEV_CFLAGS)
build/tests/%: BUILD_CFLAGS := $(TEST_CFLAGS)
build-release/%: BUILD_CFLAGS := $(RELEASE_CFLAGS)
build-release/%: BUILD_LDFLAGS := $(RELEASE_LDFLAGS)
build-asan/%: BUILD_CFLAGS := $(ASAN_CFLAGS)

# Commands: `make help` lists them with the text after ##
.PHONY: all release asan test lint format editor install clean help

all: build/kotetsu ## build for development, with warnings as errors, into build/
release: build-release/kotetsu ## build optimized and hardened into build-release/
asan: build-asan/kotetsu ## build with AddressSanitizer and UBSan into build-asan/

test: all $(TEST_PROGRAMS) ## build and run the unit tests; needs the fonts in fonts/
	@KOTETSU_FONT_DIR='$(KOTETSU_FONT_DIR)' tests/run.sh $(TEST_PROGRAMS)

lint: ## check the formatting and run clang-tidy; changes no files
	clang-format --dry-run --Werror $(FORMAT_FILES)
	clang-tidy --quiet --warnings-as-errors='*' $(MODULES) src/main.c -- $(SHARED_CFLAGS) $(DEV_CFLAGS)

format: ## format the code in place
	clang-format -i $(FORMAT_FILES)

editor: ## write compile_commands.json for clangd editors such as Zed
	@{ separator='['; \
	for file in $(MODULES) src/main.c $(TESTS); do \
		case $$file in tests/*) flags='$(TEST_CFLAGS)' ;; *) flags='$(DEV_CFLAGS)' ;; esac; \
		printf '%s\n  {"directory": "%s", "file": "%s", "command": "%s"}' "$$separator" '$(CURDIR)' "$$file" \
			"$(CC) $(SHARED_CFLAGS) $$flags -c $$file"; \
		separator=','; \
	done; \
	printf '\n]\n'; } > compile_commands.json

install: build-release/kotetsu ## install the release build to /usr/local/bin
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build-release/kotetsu $(DESTDIR)$(PREFIX)/bin/kotetsu

clean: ## delete the build directories
	rm -rf build build-release build-asan

help: ## list these commands
	@awk -F ':.*## ' '/^[a-z]+:.*## / { printf "  %-14s %s\n", ($$1 == "all" ? "make" : "make " $$1), $$2 }' $(firstword $(MAKEFILE_LIST))

# Compile rules: each build directory compiles src/ with its own flags; `make -n` shows the full commands
compile = @echo "compile $<"; $(CC) $(SHARED_CFLAGS) $(BUILD_CFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<
link = @echo "link $@"; $(CC) $(SHARED_CFLAGS) $(BUILD_CFLAGS) $(CFLAGS) $(BUILD_LDFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c
	@mkdir -p $(@D)
	$(compile)
build-release/%.o: src/%.c
	@mkdir -p $(@D)
	$(compile)
build-asan/%.o: src/%.c
	@mkdir -p $(@D)
	$(compile)

build/libkotetsu_core.a: $(MODULES:src/%.c=build/%.o)
build-release/libkotetsu_core.a: $(MODULES:src/%.c=build-release/%.o)
build-asan/libkotetsu_core.a: $(MODULES:src/%.c=build-asan/%.o)
%/libkotetsu_core.a:
	@echo "archive $@"; rm -f $@; $(AR) rcs $@ $^

build/kotetsu build-release/kotetsu build-asan/kotetsu: %/kotetsu: %/main.o %/libkotetsu_core.a
	$(link)

build/tests/unity.o: BUILD_CFLAGS := -w # third-party code, so its warnings aren't ours to fix
build/tests/unity.o: tests/unity/unity.c
	@mkdir -p $(@D)
	$(compile)
build/tests/%.o: tests/%.c
	@mkdir -p $(@D)
	$(compile)
$(TEST_PROGRAMS): build/tests/%: build/tests/%.o build/tests/unity.o build/libkotetsu_core.a
	$(link)

-include $(shell find build build-release build-asan -name '*.d' 2>/dev/null)
