#pragma once

// journald reads the "<3>", "<4>" and "<6>" prefixes as the priority; one write(2) per line
[[gnu::format(printf, 1, 2)]] void log_error(const char *format, ...);
[[gnu::format(printf, 1, 2)]] void log_warning(const char *format, ...);
[[gnu::format(printf, 1, 2)]] void log_info(const char *format, ...);
