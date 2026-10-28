#pragma once

#include "model.h"

int calendar_weekday(struct civil_date date); // 1 = Monday … 7 = Sunday
int calendar_days_between(struct civil_date from, struct civil_date to);
struct civil_date calendar_add_days(struct civil_date date, int days);
int calendar_counter_days(struct civil_date today);
struct civil_date calendar_easter(int year);
const char *calendar_polish_holiday(struct civil_date date);
const char *calendar_month_name(int month);
const char *calendar_weekday_name(int weekday);
[[nodiscard]] bool calendar_date_line(struct civil_date date, struct date_line *out);
