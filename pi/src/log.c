#include "log.h"

#include <stdarg.h>
#include <stdio.h>

[[gnu::format(printf, 2, 0)]] static void log_line(const char *priority, const char *format,
                                                   va_list arguments) {
    fputs(priority, stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
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
