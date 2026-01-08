#include "basic.h"
#include "logger.h"

int fetch_weather(void) {
  LOG_ENTER(MODULE_WEATHER);
  
  double temperature = 0;

  CURL *curl;
  CURLcode res;

  LOG_INFO(MODULE_WEATHER, "Initializing CURL handle...");
  curl = curl_easy_init();
  
  if (!curl) {
    LOG_ERROR(MODULE_WEATHER, "curl_easy_init() failed - returned NULL");
    LOG_EXIT(MODULE_WEATHER, -1);
    return -1;
  }
  LOG_DEBUG(MODULE_WEATHER, "CURL handle created at %p", (void*)curl);

  wb_string s;
  LOG_DEBUG(MODULE_WEATHER, "Initializing response string buffer...");
  init_string(&s);
  LOG_DEBUG(MODULE_WEATHER, "Response buffer initialized: ptr=%p, len=%zu", (void*)s.ptr, s.len);

  char url[200];
  snprintf(url, sizeof(url), "%s?q=%s&appid=%s&units=metric", API, CITY, APIKEY);
  LOG_INFO(MODULE_WEATHER, "Weather API URL constructed");
  LOG_DEBUG(MODULE_WEATHER, "URL: %s", url);

  LOG_DEBUG(MODULE_WEATHER, "Setting CURL options...");
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
  LOG_DEBUG(MODULE_WEATHER, "CURL options set " SYM_CHECK);

  LOG_INFO(MODULE_WEATHER, "Performing HTTP request...");
  LOG_NET_OP(MODULE_WEATHER, "curl_easy_perform()");
  res = curl_easy_perform(curl);

  if (res != CURLE_OK) {
    LOG_ERROR(MODULE_WEATHER, "curl_easy_perform() failed: %s", curl_easy_strerror(res));
    LOG_FREE(MODULE_WEATHER, "fetch_weather", "s.ptr", s.ptr);
    free(s.ptr);
    curl_easy_cleanup(curl);
    LOG_EXIT(MODULE_WEATHER, -1);
    return -1;
  }
  LOG_INFO(MODULE_WEATHER, "HTTP request completed successfully " SYM_CHECK);
  LOG_DEBUG(MODULE_WEATHER, "Response length: %zu bytes", s.len);
  LOG_DEBUG(MODULE_WEATHER, "Response (first 200 chars): %.200s", s.ptr);

  LOG_INFO(MODULE_WEATHER, "Parsing JSON response...");
  cJSON *j_total = NULL;
  cJSON *j_main = NULL;

  j_total = cJSON_Parse(s.ptr);
  if (j_total == NULL) {
    LOG_ERROR(MODULE_WEATHER, "cJSON_Parse() failed - invalid JSON");
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL) {
      LOG_ERROR(MODULE_WEATHER, "JSON error near: %.20s", error_ptr);
    }
    LOG_FREE(MODULE_WEATHER, "fetch_weather", "s.ptr", s.ptr);
    free(s.ptr);
    curl_easy_cleanup(curl);
    LOG_EXIT(MODULE_WEATHER, -1);
    return -1;
  }
  LOG_DEBUG(MODULE_WEATHER, "JSON parsed successfully");
  
  j_main = cJSON_GetObjectItem(j_total, "main");

  if (j_main) {
    LOG_DEBUG(MODULE_WEATHER, "Found 'main' object in JSON");
    cJSON *j_key = j_main->child;
    if (j_key) {
      temperature = j_key->valueint;
      LOG_INFO(MODULE_WEATHER, "Extracted temperature: %d", (int)temperature);
    } else {
      LOG_WARN(MODULE_WEATHER, "'main' object has no children");
    }
  } else {
    LOG_WARN(MODULE_WEATHER, "'main' object not found in JSON response");
    LOG_DEBUG(MODULE_WEATHER, "Full response: %s", s.ptr);
  }

  LOG_DEBUG(MODULE_WEATHER, "Cleaning up resources...");
  LOG_FREE(MODULE_WEATHER, "fetch_weather", "s.ptr", s.ptr);
  free(s.ptr);
  
  LOG_DEBUG(MODULE_WEATHER, "Deleting cJSON object...");
  cJSON_Delete(j_total);
  
  LOG_DEBUG(MODULE_WEATHER, "Cleaning up CURL handle...");
  curl_easy_cleanup(curl);
  
  LOG_INFO(MODULE_WEATHER, "Weather fetch complete - returning %d°C", (int)temperature);
  LOG_EXIT(MODULE_WEATHER, (int)temperature);
  return temperature;
}
