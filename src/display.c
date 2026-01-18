#include <time.h>

#include "EPD_2in7.h"
#include "basic.h"

#define HEIGHT EPD_2IN7_WIDTH
#define WIDTH EPD_2IN7_HEIGHT
#define IMG_SIZE EPD_2IN7_WIDTH / 8 * EPD_2IN7_HEIGHT
#define BUFFER_SIZE (256 * 1024)

int screen_shutdown(void) {
  EPD_2IN7_Sleep();
  DEV_Delay_ms(2000);  // important, at least 2s
  DEV_Module_Exit();

  return 0;
}

int screen_init(void) {
  if (DEV_Module_Init() != 0) {
    return -1;
  }

  EPD_2IN7_Init();
  EPD_2IN7_Clear();

  return 0;
}

int gen_image(UBYTE *image) {
  if ((image = (UBYTE *)malloc(IMG_SIZE)) == NULL) {
    printf("Failed to apply for background memory...\r\n");
    return 0;
  }

  char full_date[20];
  char weekday[4] = "";
  char temperature[5];
  char url[200];

  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  /* calculate full date */
  snprintf(full_date, sizeof(full_date), "%s, %d", month[tm.tm_mon],
           tm.tm_mday);

  /* calculate weekday */
  strcat(weekday, day_arr[tm.tm_wday]);

  /* calculate days since */
  char days_since[5];
  Date d_now = {tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900};
  Date d_begin = {1, 9, 2021};
  int days_since_num = get_diff(d_begin, d_now);
  sprintf(days_since, "%d", days_since_num);

  /* calculate temperature */
  int temperature_num = fetch_weather();
  sprintf(temperature, "%d", temperature_num);

  /* fetch phrase */
  snprintf(url, sizeof(url),
           "https://kefan.me/api/get_phrase?temp=%d&y=%d&m=%d&d=%d&days=%d",
           temperature_num, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
           days_since_num);

  char *sanitized_data = strstr(fetch_phrase(url), "8FJ20GMV");
  memmove(&sanitized_data[0], &sanitized_data[8], strlen(sanitized_data) - 0);

  char lang = *sanitized_data <= 0x7F ? 'e' : 'c';

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
  Paint_DrawString_EN(40 - strlen(temperature) * 10, 10, temperature, &Font24,
                      WHITE, BLACK);

  /* day since */
  Paint_DrawString_EN(260 - strlen(days_since) * 18, 10, days_since, &Font24,
                      WHITE, BLACK);

  /* phrase */
  int v_counter = 0;
  char *token = strtok(sanitized_data, "\n");

  while (token != NULL) {
    if (lang == 'c') {
      Paint_DrawString_CN(10, 50 + v_counter * 20, token, &Font12CN, BLACK,
                          WHITE);
    } else if (lang == 'e') {
      Paint_DrawString_EN(10, 50 + v_counter * 20, token, &Font16, WHITE,
                          BLACK);
    }
    token = strtok(NULL, "\n");
    v_counter++;
  }

  EPD_2IN7_Display(image);

  return 0;
}
