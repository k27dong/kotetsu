#pragma once

[[gnu::format(printf, 1, 2)]] void log_error(const char *format, ...);
[[gnu::format(printf, 1, 2)]] void log_warning(const char *format, ...);
[[gnu::format(printf, 1, 2)]] void log_info(const char *format, ...);
