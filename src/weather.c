#include "basic.h"

int fetch_weather(void) {
  double temperature = 0;

  CURL* curl;
  CURLcode res;

  if (!(curl = curl_easy_init())) return -1;

  wb_string s;
  init_string(&s);

  char url[200];
  snprintf(url, sizeof(url), "%s?q=%s&appid=%s&units=metric", API, CITY, APIKEY);

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

  res = curl_easy_perform(curl);

  if (res != CURLE_OK) return -1;

  cJSON* j_total = NULL;
  cJSON* j_main = NULL;

  j_total = cJSON_Parse(s.ptr);
  j_main = cJSON_GetObjectItem(j_total, "main");

  if (j_main) {
    cJSON *j_key = j_main->child;
    temperature = j_key->valueint;
  }

  free(s.ptr);
  cJSON_Delete(j_total);
  curl_easy_cleanup(curl);

  return temperature;
}