#pragma once

#include <stdint.h>

#include "model.h"

int64_t clock_now(void);
void clock_set_override(int64_t start_s);
bool clock_is_overridden(void);
[[nodiscard]] bool clock_parse_iso8601(const char *text, int64_t *out_s);
[[nodiscard]] bool clock_is_synced(void);
struct civil_date clock_warsaw_date(int64_t now_s);
int clock_warsaw_minute_of_day(int64_t now_s);
