#include "basic.h"
#include "logger.h"

int count_leap_years(Date d) {
  LOG_DEBUG(MODULE_DATE, "Counting leap years up to %d-%02d-%02d", d.y, d.m, d.d);
  
  int years = d.y;

  if (d.m <= 2) {
    years--;
    LOG_DEBUG(MODULE_DATE, "Month <= 2, adjusted years to %d", years);
  }

  int leap_count = years / 4 - years / 100 + years / 400;
  LOG_DEBUG(MODULE_DATE, "Leap year count: %d", leap_count);
  
  return leap_count;
}

int get_diff(Date dt1, Date dt2) {
  LOG_ENTER(MODULE_DATE);
  LOG_DEBUG(MODULE_DATE, "Calculating difference between %d-%02d-%02d and %d-%02d-%02d",
            dt1.y, dt1.m, dt1.d, dt2.y, dt2.m, dt2.d);
  
  long int n1 = dt1.y * 365 + dt1.d;
  LOG_DEBUG(MODULE_DATE, "n1 initial (year*365 + day): %ld", n1);

  for (int i = 0; i < dt1.m - 1; i++) {
    n1 += month_days[i];
  }
  LOG_DEBUG(MODULE_DATE, "n1 after adding month days: %ld", n1);

  n1 += count_leap_years(dt1);
  LOG_DEBUG(MODULE_DATE, "n1 after adding leap years: %ld", n1);

  long int n2 = dt2.y * 365 + dt2.d;
  LOG_DEBUG(MODULE_DATE, "n2 initial (year*365 + day): %ld", n2);

  for (int i = 0; i < dt2.m - 1; i++) {
    n2 += month_days[i];
  }
  LOG_DEBUG(MODULE_DATE, "n2 after adding month days: %ld", n2);

  n2 += count_leap_years(dt2);
  LOG_DEBUG(MODULE_DATE, "n2 after adding leap years: %ld", n2);

  int diff = (n2 - n1);
  LOG_INFO(MODULE_DATE, "Date difference: %d days", diff);
  LOG_EXIT(MODULE_DATE, diff);
  
  return diff;
}
