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

  printf("%s\n", s.ptr);

  if (res != CURLE_OK) return -1;

  free(s.ptr);
  curl_easy_cleanup(curl);

  return temperature;
}