#ifndef _BASIC_H_
#define _BASIC_H_

#include <stdlib.h>
#include <curl/curl.h>

#include "DEV_Config.h"
#include "GUI_Paint.h"
#include "GUI_BMPfile.h"
#include "Debug.h"

typedef struct Date {
  int d, m, y;
} Date;

int get_diff(Date dt1, Date dt2);
int count_leap_years(Date d);

int screen_init(void);
int screen_shutdown(void);
int gen_image(UBYTE*);

int fetch_weather(void);

#endif
