#pragma once

// one line on stderr in one write, prefixed "<3>", "<4>" or "<6>" for journald
[[gnu::format(printf, 1, 2)]] void log_error(const char *format, ...);
[[gnu::format(printf, 1, 2)]] void log_warning(const char *format, ...);
[[gnu::format(printf, 1, 2)]] void log_info(const char *format, ...);
