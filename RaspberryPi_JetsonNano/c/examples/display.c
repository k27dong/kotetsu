#include "EPD_Test.h"
#include "EPD_2in7.h"
#include <time.h>

#define HEIGHT EPD_2IN7_WIDTH
#define WIDTH EPD_2IN7_HEIGHT
#define IMG_SIZE EPD_2IN7_WIDTH / 8 * EPD_2IN7_HEIGHT

const static char* month[] = {
  "JAN",
  "FEB",
  "MAR",
  "APR",
  "MAY",
  "JUN",
  "JUL",
  "AUG",
  "SEP",
  "OCT",
  "NOV",
  "DEC"
};

const static char* day_arr[] = {
  "MON",
  "TUE",
  "WED",
  "THU",
  "FRI",
  "SAT",
  "SUN"
};

int screen_shutdown(void) {
  EPD_2IN7_Sleep();
  DEV_Delay_ms(2000);//important, at least 2s
  DEV_Module_Exit();

  return 0;
}

int screen_init(void) {
  if(DEV_Module_Init()!=0){
    return -1;
  }

  EPD_2IN7_Init();
  EPD_2IN7_Clear();

  return 0;
}

int calc_weekday(int y, int m, int d) {
  return (d += m < 3 ? y-- : y - 2, 23*m/9 + d + 4 + y/4- y/100 + y/400)%7;
}

int gen_image(UBYTE* image) {
  if((image = (UBYTE *) malloc(IMG_SIZE)) == NULL) {
    printf("Failed to apply for background memory...\r\n");
    return 0;
  }

  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  printf(
    "now: %d-%02d-%02d %02d:%02d:%02d\n",
    tm.tm_year + 1900,
    tm.tm_mon + 1,
    tm.tm_mday,
    tm.tm_hour,
    tm.tm_min,
    tm.tm_sec
  );

  char full_date[20];
  char temp_date[2];
  sprintf(temp_date, "%d", tm.tm_mday);
  strcat(full_date, month[tm.tm_mon]);
  strcat(full_date, ", ");
  strcat(full_date, temp_date);

  int weekday_num = calc_weekday(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  char weekday[3];
  strcat(weekday, day_arr[weekday_num - 1]);

  Paint_NewImage(image, EPD_2IN7_WIDTH, EPD_2IN7_HEIGHT, 270, WHITE);

  /* outline */
  Paint_SelectImage(image);
  Paint_Clear(WHITE);
  Paint_DrawLine(2, 40, WIDTH - 2, 40, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  Paint_DrawLine(80, 2, 80, 40, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  Paint_DrawLine(184, 2, 184, 40, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

  /* date */
  Paint_DrawString_EN(80, 20, full_date, &Font20, WHITE, BLACK);

  /* day */
  Paint_DrawString_EN(110, 0, weekday, &Font20, WHITE, BLACK);

  /* temperature */
  Paint_DrawString_EN(2, 10, "2000", &Font24, WHITE, BLACK);

  /* day since */
  Paint_DrawString_EN(190, 10, "2000", &Font24, WHITE, BLACK);

  /* phrase */

  EPD_2IN7_Display(image);
  return 0;
}

