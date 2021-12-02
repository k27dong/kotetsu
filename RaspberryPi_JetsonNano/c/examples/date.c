#include "basic.h"

const static int month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

int count_leap_years(Date d) {
  int years = d.y;

  if (d.m <= 2) years--;

  return years / 4 - years / 100 + years / 400;
}

int get_diff(Date dt1, Date dt2) {
  long int n1 = dt1.y * 365 + dt1.d;

  for (int i = 0; i < dt1.m - 1; i++) {
    n1 += month_days[i];
  }

  n1 += count_leap_years(dt1);

  long int n2 = dt2.y * 365 + dt2.d;

  for (int i = 0; i < dt2.m - 1; i++) {
    n2 += month_days[i];
  }

  n2 += count_leap_years(dt2);

  return (n2 - n1);
}