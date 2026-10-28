#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <gpiod.h>

#include "log.h"
#include "panel/board.h"

#define GPIO_CHIPS        "/dev/gpiochip*"
#define HEADER_CHIP_LABEL "pinctrl-rp1"
#define SPI_DEVICE        "/dev/spidev0.0"

static constexpr unsigned int RST_GPIO = 17;
static constexpr unsigned int PWR_GPIO = 18;
static constexpr unsigned int BUSY_GPIO = 24;
static constexpr unsigned int DC_GPIO = 25;

static constexpr uint8_t SPI_BITS_PER_WORD = 8;
static constexpr uint32_t SPI_SPEED_HZ = 10'000'000;

struct board_devices {
    struct gpiod_line_request *lines;
    int spi_fd;
};

static unsigned int gpio_for(enum board_line line) {
    switch (line) {
    case BOARD_LINE_RST:
        return RST_GPIO;
    case BOARD_LINE_DC:
        return DC_GPIO;
    case BOARD_LINE_PWR:
        return PWR_GPIO;
    }
    log_error("unknown board line %d", (int)line);
    abort();
}

static bool write_spi(void *context, const uint8_t *data, size_t length) {
    const struct board_devices *devices = context;
    const ssize_t written = write(devices->spi_fd, data, length);
    if (written < 0) {
        log_error("can't write %zu bytes to " SPI_DEVICE ": %s", length, g_strerror(errno));
        return false;
    }
    if ((size_t)written != length) {
        log_error("wrote %zd of %zu bytes to " SPI_DEVICE, written, length);
        return false;
    }
    return true;
}

static void set_line(void *context, enum board_line line, int value) {
    const struct board_devices *devices = context;
    const unsigned int gpio = gpio_for(line);
    const enum gpiod_line_value level =
            value != 0 ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
    if (gpiod_line_request_set_value(devices->lines, gpio, level) != 0) {
        log_error("can't drive GPIO %u: %s", gpio, g_strerror(errno));
        abort(); // set_line can't report it, and the panel mustn't run with a line stuck
    }
}

static int read_busy(void *context) {
    const struct board_devices *devices = context;
    const enum gpiod_line_value level = gpiod_line_request_get_value(devices->lines, BUSY_GPIO);
    if (level == GPIOD_LINE_VALUE_ERROR) {
        log_error("can't read BUSY on GPIO %u: %s", BUSY_GPIO, g_strerror(errno));
        return -1;
    }
    return level == GPIOD_LINE_VALUE_ACTIVE;
}

static void sleep_ms([[maybe_unused]] void *context, int milliseconds) {
    struct timespec remaining = {
            .tv_sec = milliseconds / 1000,
            .tv_nsec = (long)(milliseconds % 1000) * 1'000'000,
    };
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

static bool is_header_chip(struct gpiod_chip *chip, const char *path) {
    struct gpiod_chip_info *info = gpiod_chip_get_info(chip);
    if (info == nullptr) {
        log_warning("can't read %s: %s", path, g_strerror(errno));
        return false;
    }
    const bool matches = strcmp(gpiod_chip_info_get_label(info), HEADER_CHIP_LABEL) == 0;
    gpiod_chip_info_free(info);
    return matches;
}

static struct gpiod_chip *open_header_chip(void) {
    glob_t paths = {};
    struct gpiod_chip *match = nullptr;
    if (glob(GPIO_CHIPS, 0, nullptr, &paths) == 0) {
        for (size_t i = 0; i < paths.gl_pathc && match == nullptr; i++) {
            struct gpiod_chip *chip = gpiod_chip_open(paths.gl_pathv[i]);
            if (chip == nullptr) {
                log_warning("can't open %s: %s", paths.gl_pathv[i], g_strerror(errno));
            } else if (is_header_chip(chip, paths.gl_pathv[i])) {
                match = chip;
            } else {
                gpiod_chip_close(chip);
            }
        }
    }
    globfree(&paths);
    if (match == nullptr) {
        log_error("no GPIO chip is labelled " HEADER_CHIP_LABEL);
    }
    return match;
}

static struct gpiod_line_request *request_lines(struct gpiod_chip *chip) {
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *line_config = gpiod_line_config_new();
    struct gpiod_request_config *request_config = gpiod_request_config_new();
    struct gpiod_line_request *lines = nullptr;
    if (settings == nullptr || line_config == nullptr || request_config == nullptr) {
        log_error("can't set up the GPIO request: %s", g_strerror(errno));
        goto out;
    }
    if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) != 0 ||
        gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_ACTIVE) != 0 ||
        gpiod_line_config_add_line_settings(line_config, &RST_GPIO, 1, settings) != 0 ||
        gpiod_line_config_add_line_settings(line_config, &PWR_GPIO, 1, settings) != 0 ||
        gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE) != 0 ||
        gpiod_line_config_add_line_settings(line_config, &DC_GPIO, 1, settings) != 0 ||
        gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT) != 0 ||
        gpiod_line_config_add_line_settings(line_config, &BUSY_GPIO, 1, settings) != 0) {
        log_error("can't configure the panel's GPIO lines: %s", g_strerror(errno));
        goto out;
    }
    gpiod_request_config_set_consumer(request_config, "kotetsu");
    lines = gpiod_chip_request_lines(chip, request_config, line_config);
    if (lines == nullptr) {
        log_error("can't request GPIO %u, %u, %u and %u: %s", RST_GPIO, PWR_GPIO, DC_GPIO,
                  BUSY_GPIO, g_strerror(errno));
    }
out:
    gpiod_request_config_free(request_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(settings);
    return lines;
}

static int open_spi(void) {
    const int fd = open(SPI_DEVICE, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        log_error("can't open " SPI_DEVICE ": %s", g_strerror(errno));
        return -1;
    }
    const uint8_t mode = SPI_MODE_0;
    const uint8_t bits_per_word = SPI_BITS_PER_WORD;
    const uint32_t speed_hz = SPI_SPEED_HZ;
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) != 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) != 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) != 0) {
        log_error("can't set up " SPI_DEVICE ": %s", g_strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

bool board_linux_open(struct board_ops *out) {
    struct gpiod_chip *chip = open_header_chip();
    if (chip == nullptr) {
        return false;
    }
    struct gpiod_line_request *lines = request_lines(chip);
    gpiod_chip_close(chip); // the request holds its own file descriptor
    if (lines == nullptr) {
        return false;
    }
    const int spi_fd = open_spi();
    if (spi_fd < 0) {
        gpiod_line_request_release(lines);
        return false;
    }
    struct board_devices *devices = g_new(struct board_devices, 1);
    *devices = (struct board_devices){.lines = lines, .spi_fd = spi_fd};
    *out = (struct board_ops){
            .context = devices,
            .spi_write = write_spi,
            .set_line = set_line,
            .read_busy = read_busy,
            .sleep_ms = sleep_ms,
    };
    return true;
}

void board_linux_close(struct board_ops *board) {
    struct board_devices *devices = board->context;
    gpiod_line_request_release(devices->lines);
    if (close(devices->spi_fd) != 0) {
        log_warning("can't close " SPI_DEVICE ": %s", g_strerror(errno));
    }
    g_free(devices);
    *board = (struct board_ops){};
}
