/* the panel driver: GPIO lines and SPI on the Pi's header */
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

#include <gpiod.h>

#include "screen/board.h"
#include "screen/epd.h"
#include "shared/log.h"

#define GPIO_CHIPS        "/dev/gpiochip*"
#define HEADER_CHIP_LABEL "pinctrl-rp1"
#define SPI_DEVICE        "/dev/spidev0.0"

static constexpr unsigned int RST_GPIO = 17;
static constexpr unsigned int PWR_GPIO = 18;
static constexpr unsigned int BUSY_GPIO = 24;
static constexpr unsigned int DC_GPIO = 25;

static constexpr uint8_t SPI_BITS_PER_WORD = 8;
static constexpr uint32_t SPI_SPEED_HZ = 10'000'000;
static constexpr size_t SPI_WRITE_MAX_BYTES = 4096;

static constexpr int RESET_PULSE_MS = 2;
static constexpr int BUSY_POLL_MS = 10;
static constexpr int BUSY_LIMIT_MS = 10'000;

struct board {
    struct gpiod_line_request *lines;
    int spi_fd;
};

static void sleep_ms(int milliseconds) {
    struct timespec remaining = {
            .tv_sec = milliseconds / 1000,
            .tv_nsec = (long)(milliseconds % 1000) * 1'000'000,
    };
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

static bool drive(struct board *board, unsigned int gpio, bool high) {
    const enum gpiod_line_value level = high ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
    if (gpiod_line_request_set_value(board->lines, gpio, level) != 0) {
        log_error("can't drive GPIO %u: %s", gpio, strerror(errno));
        return false;
    }
    return true;
}

static bool pulse_reset(struct board *board) {
    if (!drive(board, RST_GPIO, false)) {
        return false;
    }
    sleep_ms(RESET_PULSE_MS);
    return drive(board, RST_GPIO, true);
}

static bool write_spi(struct board *board, const uint8_t *data, size_t length) {
    for (size_t sent = 0; sent < length;) {
        size_t chunk = length - sent;
        if (chunk > SPI_WRITE_MAX_BYTES) {
            chunk = SPI_WRITE_MAX_BYTES;
        }
        const ssize_t written = write(board->spi_fd, data + sent, chunk);
        if (written < 0) {
            log_error("can't write %zu bytes to " SPI_DEVICE ": %s", chunk, strerror(errno));
            return false;
        }
        if ((size_t)written != chunk) {
            log_error("wrote %zd of %zu bytes to " SPI_DEVICE, written, chunk);
            return false;
        }
        sent += chunk;
    }
    return true;
}

static bool wait_while_busy(struct board *board, int last_command) {
    /* poll BUSY */
    for (int waited_ms = 0; waited_ms < BUSY_LIMIT_MS; waited_ms += BUSY_POLL_MS) {
        sleep_ms(BUSY_POLL_MS); // BUSY can lag the command it reports on
        const struct busy_reading busy = board_read_busy(board);
        if (!busy.ok) {
            return false;
        }
        if (!busy.busy) {
            return true;
        }
    }

    /* give up after 10 s */
    if (last_command < 0) {
        log_error("the panel stayed busy %d ms after the reset", BUSY_LIMIT_MS);
    } else {
        log_error("the panel stayed busy %d ms after command 0x%02X", BUSY_LIMIT_MS, last_command);
    }
    (void)pulse_reset(board); // a reset is the only way out of a stuck update
    return false;
}

bool board_run(struct board *board, const struct epd_steps *steps) {
    int last_command = -1;
    for (size_t i = 0; i < steps->count; i++) {
        const struct epd_step *step = &steps->steps[i];
        bool ok = false;

        /* send the step */
        switch (step->kind) {
        case EPD_RESET:
            ok = pulse_reset(board);
            break;
        case EPD_COMMAND:
            ok = drive(board, DC_GPIO, false) && write_spi(board, &step->byte, 1);
            last_command = step->byte;
            break;
        case EPD_DATA:
            ok = drive(board, DC_GPIO, true) && write_spi(board, step->data, step->length);
            break;
        case EPD_WAIT_IDLE:
            ok = wait_while_busy(board, last_command);
            break;
        case EPD_PAUSE:
            sleep_ms(step->milliseconds);
            ok = true;
            break;
        }

        /* stop at the first failure */
        if (!ok) {
            return false; // a panel in an unknown state gets nothing more
        }
    }
    return true;
}

struct busy_reading board_read_busy(struct board *board) {
    const enum gpiod_line_value level = gpiod_line_request_get_value(board->lines, BUSY_GPIO);
    if (level == GPIOD_LINE_VALUE_ERROR) {
        log_error("can't read BUSY on GPIO %u: %s", BUSY_GPIO, strerror(errno));
        return (struct busy_reading){};
    }
    return (struct busy_reading){.ok = true, .busy = level == GPIOD_LINE_VALUE_ACTIVE};
}

static bool is_header_chip(struct gpiod_chip *chip, const char *path) {
    struct gpiod_chip_info *info = gpiod_chip_get_info(chip);
    if (info == nullptr) {
        log_warning("can't read %s: %s", path, strerror(errno));
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
                log_warning("can't open %s: %s", paths.gl_pathv[i], strerror(errno));
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
    /* make the configurations */
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *line_config = gpiod_line_config_new();
    struct gpiod_request_config *request_config = gpiod_request_config_new();
    struct gpiod_line_request *lines = nullptr;
    if (settings == nullptr || line_config == nullptr || request_config == nullptr) {
        log_error("can't set up the GPIO request: %s", strerror(errno));
        goto out;
    }

    /* set up the four lines */
    if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) != 0 ||
        gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_ACTIVE) != 0 ||
        gpiod_line_config_add_line_settings(line_config, &RST_GPIO, 1, settings) != 0 ||
        gpiod_line_config_add_line_settings(line_config, &PWR_GPIO, 1, settings) != 0 ||
        gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE) != 0 ||
        gpiod_line_config_add_line_settings(line_config, &DC_GPIO, 1, settings) != 0 ||
        gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT) != 0 ||
        gpiod_line_config_add_line_settings(line_config, &BUSY_GPIO, 1, settings) != 0) {
        log_error("can't configure the panel's GPIO lines: %s", strerror(errno));
        goto out;
    }

    /* request the lines */
    gpiod_request_config_set_consumer(request_config, "kotetsu");
    lines = gpiod_chip_request_lines(chip, request_config, line_config);
    if (lines == nullptr) {
        log_error("can't request GPIO %u, %u, %u and %u: %s", RST_GPIO, PWR_GPIO, DC_GPIO,
                  BUSY_GPIO, strerror(errno));
    }

out:
    /* free the configurations */
    gpiod_request_config_free(request_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(settings);
    return lines;
}

static int open_spi(void) {
    /* open the device */
    const int fd = open(SPI_DEVICE, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        log_error("can't open " SPI_DEVICE ": %s", strerror(errno));
        return -1;
    }

    /* set it up */
    const uint8_t mode = SPI_MODE_0;
    const uint8_t bits_per_word = SPI_BITS_PER_WORD;
    const uint32_t speed_hz = SPI_SPEED_HZ;
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) != 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) != 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) != 0) {
        log_error("can't set up " SPI_DEVICE ": %s", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

struct board *board_open(void) {
    /* request the GPIO lines */
    struct gpiod_chip *chip = open_header_chip();
    if (chip == nullptr) {
        return nullptr;
    }
    struct gpiod_line_request *lines = request_lines(chip);
    gpiod_chip_close(chip); // the request holds its own file descriptor
    if (lines == nullptr) {
        return nullptr;
    }

    /* open SPI */
    const int spi_fd = open_spi();
    if (spi_fd < 0) {
        gpiod_line_request_release(lines);
        return nullptr;
    }

    /* make the board */
    struct board *board = malloc(sizeof *board);
    if (board == nullptr) {
        log_error("out of memory opening the board");
        abort();
    }
    *board = (struct board){.lines = lines, .spi_fd = spi_fd};
    return board;
}

void board_close(struct board *board) {
    gpiod_line_request_release(board->lines);
    if (close(board->spi_fd) != 0) {
        log_warning("can't close " SPI_DEVICE ": %s", strerror(errno));
    }
    free(board);
}
