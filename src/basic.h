#ifndef _BASIC_H_
#define _BASIC_H_

#include <stdlib.h>
#include <curl/curl.h>

typedef struct Date {
  int d, m, y;
} Date;

typedef struct wb_string {
  char* ptr;
  size_t len;
} wb_string;

#include "DEV_Config.h"
#include "GUI_Paint.h"
#include "GUI_BMPfile.h"
#include "Debug.h"
#include "secret.h"

int get_diff(Date dt1, Date dt2);
int count_leap_years(Date d);

int screen_init(void);
int screen_shutdown(void);
int gen_image(UBYTE*);

int fetch_weather(void);

void init_string(wb_string*);
size_t writefunc(void*, size_t, size_t, wb_string*);

#endif
