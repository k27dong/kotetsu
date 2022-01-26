#include "basic.h"

char* fetch_phrase(char* url) {
  CURL* curl;
  CURLcode res;

  if (!(curl = curl_easy_init())) return NULL;

  wb_string s;
  init_string(&s);

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

  res = curl_easy_perform(curl);

  if (res != CURLE_OK) return NULL;

  curl_easy_cleanup(curl);

  return s.ptr;
}