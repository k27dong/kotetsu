#include <time.h>

#include "EPD_2in7.h"
#include "basic.h"
#include "logger.h"

#define HEIGHT EPD_2IN7_WIDTH
#define WIDTH EPD_2IN7_HEIGHT
#define IMG_SIZE EPD_2IN7_WIDTH / 8 * EPD_2IN7_HEIGHT
#define BUFFER_SIZE (256 * 1024)

int screen_shutdown(void) {
  LOG_ENTER(MODULE_DISPLAY);
  
  LOG_INFO(MODULE_DISPLAY, "Putting e-paper display to sleep mode...");
  LOG_HW_OP(MODULE_DISPLAY, "EPD_2IN7_Sleep()");
  EPD_2IN7_Sleep();
  LOG_DEBUG(MODULE_DISPLAY, "Sleep command sent to display");
  
  LOG_INFO(MODULE_DISPLAY, "Waiting 2000ms for display to stabilize...");
  DEV_Delay_ms(2000);  // important, at least 2s
  LOG_DEBUG(MODULE_DISPLAY, "Delay complete");
  
  LOG_INFO(MODULE_DISPLAY, "Calling DEV_Module_Exit()...");
  LOG_HW_OP(MODULE_DISPLAY, "DEV_Module_Exit()");
  DEV_Module_Exit();
  LOG_DEBUG(MODULE_DISPLAY, "Module exit complete");

  LOG_EXIT(MODULE_DISPLAY, 0);
  return 0;
}

int screen_init(void) {
  LOG_ENTER(MODULE_DISPLAY);
  
  LOG_INFO(MODULE_DISPLAY, "Initializing device module...");
  LOG_HW_OP(MODULE_DISPLAY, "DEV_Module_Init()");
  
  if (DEV_Module_Init() != 0) {
    LOG_FATAL(MODULE_DISPLAY, "DEV_Module_Init() failed!");
    LOG_EXIT(MODULE_DISPLAY, -1);
    return -1;
  }
  LOG_INFO(MODULE_DISPLAY, "Device module initialized " SYM_CHECK);

  LOG_INFO(MODULE_DISPLAY, "Initializing EPD 2.7\" display...");
  LOG_HW_OP(MODULE_DISPLAY, "EPD_2IN7_Init()");
  EPD_2IN7_Init();
  LOG_DEBUG(MODULE_DISPLAY, "EPD_2IN7_Init() complete");
  
  LOG_INFO(MODULE_DISPLAY, "Clearing display...");
  LOG_HW_OP(MODULE_DISPLAY, "EPD_2IN7_Clear()");
  EPD_2IN7_Clear();
  LOG_DEBUG(MODULE_DISPLAY, "Display cleared");

  LOG_EXIT(MODULE_DISPLAY, 0);
  return 0;
}

int gen_image(UBYTE **image_ptr) {
  LOG_ENTER(MODULE_DISPLAY);
  
  /* Validate input pointer */
  if (image_ptr == NULL) {
    LOG_FATAL(MODULE_DISPLAY, "image_ptr is NULL - cannot allocate image!");
    LOG_EXIT(MODULE_DISPLAY, -1);
    return -1;
  }
  
  LOG_DEBUG(MODULE_DISPLAY, "Image dimensions: %d x %d", WIDTH, HEIGHT);
  LOG_DEBUG(MODULE_DISPLAY, "Image buffer size: %d bytes", IMG_SIZE);
  
  /* Allocate image buffer */
  LOG_INFO(MODULE_DISPLAY, "Allocating image buffer (%d bytes)...", IMG_SIZE);
  *image_ptr = (UBYTE *)malloc(IMG_SIZE);
  LOG_MALLOC(MODULE_DISPLAY, "gen_image", "image", *image_ptr, IMG_SIZE);
  
  if (*image_ptr == NULL) {
    LOG_FATAL(MODULE_DISPLAY, "Failed to allocate %d bytes for image buffer!", IMG_SIZE);
    LOG_EXIT(MODULE_DISPLAY, -1);
    return -1;
  }
  LOG_INFO(MODULE_DISPLAY, "Image buffer allocated at %p " SYM_CHECK, (void*)*image_ptr);
  
  UBYTE *image = *image_ptr;  /* Local pointer for convenience */

  char full_date[20];
  char weekday[4] = "";
  char temperature[5];
  char url[200];

  LOG_INFO(MODULE_DISPLAY, "Getting current date/time...");
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  LOG_DEBUG(MODULE_DISPLAY, "Current time: %d-%02d-%02d %02d:%02d:%02d",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);

  /* calculate full date */
  snprintf(full_date, sizeof(full_date), "%s, %d", month[tm.tm_mon],
           tm.tm_mday);
  LOG_DEBUG(MODULE_DISPLAY, "Full date string: '%s'", full_date);

  /* calculate weekday */
  strcat(weekday, day_arr[tm.tm_wday]);
  LOG_DEBUG(MODULE_DISPLAY, "Weekday: '%s'", weekday);

  /* calculate days since */
  char days_since[5];
  Date d_now = {tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900};
  Date d_begin = {1, 9, 2021};
  LOG_DEBUG(MODULE_DISPLAY, "Calculating days since %d-%02d-%02d", 
            d_begin.y, d_begin.m, d_begin.d);
  int days_since_num = get_diff(d_begin, d_now);
  sprintf(days_since, "%d", days_since_num);
  LOG_INFO(MODULE_DISPLAY, "Days since start: %d", days_since_num);

  /* calculate temperature */
  logger_separator();
  LOG_INFO(MODULE_DISPLAY, "Fetching weather data...");
  LOG_NET_OP(MODULE_DISPLAY, "Calling fetch_weather()...");
  int temperature_num = fetch_weather();
  sprintf(temperature, "%d", temperature_num);
  LOG_INFO(MODULE_DISPLAY, "Temperature: %d°C", temperature_num);
  logger_separator();

  /* fetch phrase */
  LOG_INFO(MODULE_DISPLAY, "Building phrase API URL...");
  snprintf(url, sizeof(url),
           "http://kdwebsite-34bi.shuttle.app/api/get_phrase?temp=%d&y=%d&m=%d&d=%d&days=%d",
           temperature_num, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
           days_since_num);
  LOG_DEBUG(MODULE_DISPLAY, "Phrase URL: %s", url);

  LOG_INFO(MODULE_DISPLAY, "Fetching phrase from API...");
  LOG_NET_OP(MODULE_DISPLAY, "Calling fetch_phrase()...");
  char *phrase_response = fetch_phrase(url);
  
  if (phrase_response == NULL) {
    LOG_ERROR(MODULE_DISPLAY, "fetch_phrase() returned NULL!");
    LOG_EXIT(MODULE_DISPLAY, -1);
    return -1;
  }
  LOG_DEBUG(MODULE_DISPLAY, "Phrase response received at %p", (void*)phrase_response);
  
  LOG_INFO(MODULE_DISPLAY, "Parsing phrase response...");
  char *sanitized_data = strstr(phrase_response, "8FJ20GMV");
  
  if (sanitized_data == NULL) {
    LOG_ERROR(MODULE_DISPLAY, "Failed to find marker '8FJ20GMV' in phrase response!");
    LOG_DEBUG(MODULE_DISPLAY, "Raw response: %.100s...", phrase_response);
    free(phrase_response);
    LOG_EXIT(MODULE_DISPLAY, -1);
    return -1;
  }
  
  memmove(&sanitized_data[0], &sanitized_data[8], strlen(sanitized_data) - 0);
  LOG_DEBUG(MODULE_DISPLAY, "Sanitized phrase data: %.50s...", sanitized_data);

  char lang = *sanitized_data <= 0x7F ? 'e' : 'c';
  LOG_INFO(MODULE_DISPLAY, "Detected language: %s", lang == 'e' ? "English" : "Chinese");

  /******************* DRAW BEGIN *******************/
  logger_section_start("DRAWING");

  LOG_INFO(MODULE_DISPLAY, "Creating new paint image...");
  LOG_DEBUG(MODULE_DISPLAY, "Paint_NewImage(image=%p, w=%d, h=%d, rotate=270, color=WHITE)",
            (void*)image, EPD_2IN7_WIDTH, EPD_2IN7_HEIGHT);
  Paint_NewImage(image, EPD_2IN7_WIDTH, EPD_2IN7_HEIGHT, 270, WHITE);

  /* outline */
  LOG_INFO(MODULE_DISPLAY, "Drawing outline and layout...");
  Paint_SelectImage(image);
  Paint_Clear(WHITE);
  LOG_DEBUG(MODULE_DISPLAY, "Drawing horizontal line at y=40");
  Paint_DrawLine(2, 40, WIDTH - 2, 40, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  LOG_DEBUG(MODULE_DISPLAY, "Drawing vertical line at x=80");
  Paint_DrawLine(80, 2, 80, 40, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  LOG_DEBUG(MODULE_DISPLAY, "Drawing vertical line at x=184");
  Paint_DrawLine(184, 2, 184, 40, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  LOG_DEBUG(MODULE_DISPLAY, "Outline complete " SYM_CHECK);

  /* date */
  LOG_DEBUG(MODULE_DISPLAY, "Drawing date: '%s'", full_date);
  Paint_DrawString_EN(82, 20, full_date, &Font20, WHITE, BLACK);

  /* day */
  LOG_DEBUG(MODULE_DISPLAY, "Drawing weekday: '%s'", weekday);
  Paint_DrawString_EN(110, 0, weekday, &Font20, WHITE, BLACK);

  /* temperature */
  LOG_DEBUG(MODULE_DISPLAY, "Drawing temperature: '%s'", temperature);
  Paint_DrawString_EN(40 - strlen(temperature) * 10, 10, temperature, &Font24,
                      WHITE, BLACK);

  /* day since */
  LOG_DEBUG(MODULE_DISPLAY, "Drawing days since: '%s'", days_since);
  Paint_DrawString_EN(260 - strlen(days_since) * 18, 10, days_since, &Font24,
                      WHITE, BLACK);

  /* phrase */
  LOG_INFO(MODULE_DISPLAY, "Drawing phrase text...");
  int v_counter = 0;
  char *token = strtok(sanitized_data, "\n");

  while (token != NULL) {
    LOG_DEBUG(MODULE_DISPLAY, "Drawing line %d: '%.30s...'", v_counter, token);
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
  LOG_INFO(MODULE_DISPLAY, "Drew %d lines of phrase text", v_counter);

  logger_section_end("DRAWING");

  LOG_INFO(MODULE_DISPLAY, "Sending image to display...");
  LOG_HW_OP(MODULE_DISPLAY, "EPD_2IN7_Display(image=%p)", (void*)image);
  EPD_2IN7_Display(image);
  LOG_INFO(MODULE_DISPLAY, "Image displayed successfully " SYM_CHECK);

  /* Free the phrase response (we still own it from fetch_phrase) */
  LOG_FREE(MODULE_DISPLAY, "gen_image", "phrase_response", phrase_response);
  free(phrase_response);

  LOG_EXIT(MODULE_DISPLAY, 0);
  return 0;
}
