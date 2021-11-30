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

  /* calculate full date */
  char full_date[20];
  char temp_date[2];
  sprintf(temp_date, "%d", tm.tm_mday);
  strcat(full_date, month[tm.tm_mon]);
  strcat(full_date, ", ");
  strcat(full_date, temp_date);

  /* calculate weekday */
  int weekday_num = calc_weekday(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  char weekday[3] = "";
  strcat(weekday, day_arr[weekday_num - 1]);

  /* calculate days since */
  char days_since[4];
  Date d_now = {tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900};
  Date d_begin = {1, 9, 2021};
  int days_since_num = get_diff(d_begin, d_now);
  sprintf(days_since, "%d", days_since_num);

  /* calculate temperature */
  int temperature_num = fetch_weather();
  char temperature[5];
  sprintf(temperature, "%d", temperature_num);

  /******************* DRAW BEGIN *******************/

  Paint_NewImage(image, EPD_2IN7_WIDTH, EPD_2IN7_HEIGHT, 270, WHITE);

  /* outline */
  Paint_SelectImage(image);
  Paint_Clear(WHITE);
  Paint_DrawLine(2, 40, WIDTH - 2, 40, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  Paint_DrawLine(80, 2, 80, 40, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  Paint_DrawLine(184, 2, 184, 40, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

  /* date */
  Paint_DrawString_EN(82, 20, full_date, &Font20, WHITE, BLACK);

  /* day */
  Paint_DrawString_EN(110, 0, weekday, &Font20, WHITE, BLACK);

  /* temperature */
  Paint_DrawString_EN(40 - strlen(temperature) * 10, 10, temperature, &Font24, WHITE, BLACK);

  /* day since */
  Paint_DrawString_EN(260 - strlen(days_since) * 18, 10, days_since, &Font24, WHITE, BLACK);

  /* phrase */
  // char* cn_phrase = "萧瑟的风雨中等待我的心情";
  Paint_DrawString_CN(10, 50, "当田野还有百合，", &Font12CN, BLACK, WHITE);
  Paint_DrawString_CN(10, 70, "天空还有鸟群", &Font12CN, BLACK, WHITE);
  Paint_DrawString_CN(10, 90, "当你还有一张大弓、满袋好箭", &Font12CN, BLACK, WHITE);
  Paint_DrawString_CN(10, 110, "该忘记的早就忘记", &Font12CN, BLACK, WHITE);
  Paint_DrawString_CN(10, 130, "该留下的永远留下", &Font12CN, BLACK, WHITE);
  // Paint_DrawString_CN(10, 150, "这个世界上居然有如此众多的绿色", &Font12CN, BLACK, WHITE);

  EPD_2IN7_Display(image);

  return 0;
}

