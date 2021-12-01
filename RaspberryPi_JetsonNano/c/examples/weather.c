#include "EPD_Test.h"

#define BUFFER_SIZE  (256 * 1024)

char* my_data;
size_t data_len;

int f_to_c(double f) {
  return (int)((f - 32) * 5 / 9);
}

size_t data_writer(char *buf, size_t size, size_t items, void* ptr) {
  data_len = data_len + (size * items);
  my_data = (char *) realloc(my_data, data_len);
  strcat(my_data, buf);
  return size * items;
}

int fetch_weather(void) {
  CURL* curl;
  CURLcode res;

  double temperature = 0;

  curl = curl_easy_init();

  if (!curl) return -1;

  data_len = 10;
  my_data = (char*) malloc(data_len);

  curl_easy_setopt(curl, CURLOPT_URL, "https://community-open-weather-map.p.rapidapi.com/weather?q=Toronto%2CCanada&lat=0&lon=0&callback=test&id=2172797&lang=null&units=imperial&mode=xml");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, data_writer);

  struct curl_slist *headers = NULL;

  headers = curl_slist_append(headers, "x-rapidapi-host: community-open-weather-map.p.rapidapi.com");
  headers = curl_slist_append(headers, "x-rapidapi-key: 25f2187702mshc2c6e1a7fc1bd59p10b61ejsn6ec7374dbe01");

  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  res = curl_easy_perform(curl);

  if (res != CURLE_OK) return -1;

  char* temp = strstr(my_data, "temp\":");
  strtok(temp, ",");
  memmove(&temp[0], &temp[6], strlen(temp) - 0);
  temperature = strtod(temp, NULL);

  free(my_data);
  curl_easy_cleanup(curl);

  return f_to_c(temperature);
}