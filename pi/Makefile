SANITIZE ?= 0
RELEASE ?= 0
PREFIX ?= /usr/local
ifeq ($(SANITIZE),1)
ifeq ($(RELEASE),1)
$(error SANITIZE=1 and RELEASE=1 can't be combined)
endif
BUILD ?= build-asan
else ifeq ($(RELEASE),1)
BUILD ?= build-release
else
BUILD ?= build
endif

SRCS := \
	src/log.c

PACKAGES := pangocairo pangoft2 fontconfig glib-2.0 cairo libcurl libcjson

SYSTEM := $(shell uname -s)
ifeq ($(SYSTEM),Linux)
PACKAGES += libgpiod
else ifeq ($(SYSTEM),Darwin)
SDKROOT ?= $(shell xcrun --show-sdk-path)
export SDKROOT
PKG_CONFIG_PATH := $(shell brew --prefix curl)/lib/pkgconfig$(if $(PKG_CONFIG_PATH),:$(PKG_CONFIG_PATH))
endif

# Make 3.81's $(shell) sees only the starting environment, so the path is passed in.
PKG_CONFIG := PKG_CONFIG_PATH='$(PKG_CONFIG_PATH)' pkg-config
MISSING_PACKAGES := $(strip $(foreach package,$(PACKAGES), \
	$(if $(shell $(PKG_CONFIG) --exists $(package) && echo found),,$(package))))
ifneq ($(MISSING_PACKAGES),)
$(error pkg-config can't find: $(MISSING_PACKAGES))
endif
PACKAGE_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PACKAGES))
LDLIBS := $(shell $(PKG_CONFIG) --libs $(PACKAGES))

# glibc hides POSIX functions under -std=c23 unless _DEFAULT_SOURCE is defined.
CFLAGS := -std=c23 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wvla \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wimplicit-fallthrough \
	-finput-charset=UTF-8 -fexec-charset=UTF-8 \
	-D_DEFAULT_SOURCE -Isrc $(patsubst -I%,-isystem %,$(PACKAGE_CFLAGS)) -MMD -MP
DEVELOPMENT_CFLAGS := $(CFLAGS) -Werror
LDFLAGS :=
ifeq ($(RELEASE),1)
CFLAGS += -O2 -fhardened -fstrict-flex-arrays=3 -mbranch-protection=standard
LDFLAGS += -Wl,-z,noexecstack -Wl,--as-needed
else
CFLAGS := $(DEVELOPMENT_CFLAGS)
endif
ifeq ($(SANITIZE),1)
CFLAGS += -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
endif

OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
FORMAT_FILES := $(sort $(shell find src -name '*.[ch]'))

.PHONY: all lib format format-check tidy check flags install clean

all: $(BUILD)/kotetsu

lib: $(BUILD)/libkotetsu_core.a

$(BUILD)/kotetsu: $(BUILD)/main.o $(BUILD)/libkotetsu_core.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/libkotetsu_core.a: $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

format:
	clang-format -i $(FORMAT_FILES)

format-check:
	clang-format --dry-run --Werror $(FORMAT_FILES)

tidy:
	clang-tidy $(SRCS) src/main.c -- $(DEVELOPMENT_CFLAGS)

check: format-check all

flags:
	@echo 'CFLAGS=$(CFLAGS)'
	@echo 'LDLIBS=$(LDLIBS)'

install: $(BUILD)/kotetsu
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BUILD)/kotetsu $(DESTDIR)$(PREFIX)/bin/kotetsu

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d) $(BUILD)/main.d
