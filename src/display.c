#include <time.h>

#include "basic.h"
#include "EPD_2in7.h"

#define HEIGHT EPD_2IN7_WIDTH
#define WIDTH EPD_2IN7_HEIGHT
#define IMG_SIZE EPD_2IN7_WIDTH / 8 * EPD_2IN7_HEIGHT
#define BUFFER_SIZE  (256 * 1024)

char* phrase_data;
size_t phrase_len;

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
  "SUN",
  "MON",
  "TUE",
  "WED",
  "THU",
  "FRI",
  "SAT"
};

int screen_shutdown(void) {
  EPD_2IN7_Sleep();
  DEV_Delay_ms(2000);//important, at least 2s
  DEV_Module_Exit();

  return 0;
}

int screen_init(void) {
  if (DEV_Module_Init()!=0) {
    return -1;
  }

  EPD_2IN7_Init();
  EPD_2IN7_Clear();

  return 0;
}

size_t phrase_writer(char *buf, size_t size, size_t items, void* ptr) {
  phrase_len = phrase_len + (size * items);
  phrase_data = (char *) realloc(phrase_data, phrase_len);
  strcat(phrase_data, buf);
  return size * items;
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
  memset(full_date, '\0', sizeof(char)*20);
  sprintf(temp_date, "%d", tm.tm_mday);
  strcat(full_date, month[tm.tm_mon]);
  strcat(full_date, ", ");
  strcat(full_date, temp_date);

  /* calculate weekday */
  char weekday[4] = "";
  strcat(weekday, day_arr[tm.tm_wday]);

  /* calculate days since */
  char days_since[5];
  Date d_now = {tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900};
  Date d_begin = {1, 9, 2021};
  int days_since_num = get_diff(d_begin, d_now);
  sprintf(days_since, "%d", days_since_num);

  /* calculate temperature */
  int temperature_num = fetch_weather();
  char temperature[5];
  sprintf(temperature, "%d", temperature_num);

  /* fetch phrase */
  CURL* curl;
  CURLcode res;

  curl = curl_easy_init();

  phrase_len = 10;
  phrase_data = (char*) malloc(phrase_len);

  /* construct url */
  char url[200] = "http://kefan.me/api/get_phrase?";
  char holder[20] = "";

  sprintf(holder, "temp=%d&", temperature_num);
  strcat(url, holder);
  sprintf(holder, "y=%d&", tm.tm_year + 1900);
  strcat(url, holder);
  sprintf(holder, "m=%d&", tm.tm_mon + 1);
  strcat(url, holder);
  sprintf(holder, "d=%d&", tm.tm_mday);
  strcat(url, holder);
  sprintf(holder, "days=%d", days_since_num);
  strcat(url, holder);

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, phrase_writer);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

  struct curl_slist *headers = NULL;
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  res = curl_easy_perform(curl);

  if (res != CURLE_OK) return -1;

  char* sanitized_data = strstr(phrase_data, "8FJ20GMV");
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
  Paint_DrawString_EN(40 - strlen(temperature) * 10, 10, temperature, &Font24, WHITE, BLACK);

  /* day since */
  Paint_DrawString_EN(260 - strlen(days_since) * 18, 10, days_since, &Font24, WHITE, BLACK);

  /* phrase */
  int v_counter = 0;
  char* token = strtok(sanitized_data, "\n");

  while( token != NULL ) {
    if (lang == 'c') {
      Paint_DrawString_CN(10, 50 + v_counter * 20, token, &Font12CN, BLACK, WHITE);
    }
    else if (lang == 'e') {
      Paint_DrawString_EN(10, 50 + v_counter * 20, token, &Font16, WHITE, BLACK);
    }
    token = strtok(NULL, "\n");
    v_counter++;
  }

  EPD_2IN7_Display(image);
  curl_easy_cleanup(curl);

  return 0;
}

