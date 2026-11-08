#include "shared/log.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

static constexpr size_t MESSAGE_BYTES = 4'096;

[[gnu::format(printf, 2, 0)]] static void log_line(const char *priority, const char *format,
                                                   va_list arguments) {
    char message[MESSAGE_BYTES];
    vsnprintf(message, sizeof message, format, arguments); // a longer message is cut short
    char line[sizeof "<3>\n" + MESSAGE_BYTES];
    const int length = snprintf(line, sizeof line, "%s%s\n", priority, message);

    // the whole line in one write, so lines from threads logging at once never mix
    for (int sent = 0; sent < length;) {
        const ssize_t written = write(STDERR_FILENO, line + sent, (size_t)(length - sent));
        if (written <= 0) {
            return; // stderr is gone, and there is nowhere left to say so
        }
        sent += (int)written;
    }
}

void log_error(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    log_line("<3>", format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    log_line("<4>", format, arguments);
    va_end(arguments);
}

void log_info(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    log_line("<6>", format, arguments);
    va_end(arguments);
}
