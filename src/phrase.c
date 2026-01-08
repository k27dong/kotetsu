#include "basic.h"
#include "logger.h"

char *fetch_phrase(char *url) {
  LOG_ENTER(MODULE_PHRASE);
  
  if (url == NULL) {
    LOG_ERROR(MODULE_PHRASE, "URL parameter is NULL!");
    LOG_EXIT_PTR(MODULE_PHRASE, NULL);
    return NULL;
  }
  LOG_DEBUG(MODULE_PHRASE, "URL: %s", url);
  
  CURL *curl;
  CURLcode res;

  LOG_INFO(MODULE_PHRASE, "Initializing CURL handle...");
  curl = curl_easy_init();
  
  if (!curl) {
    LOG_ERROR(MODULE_PHRASE, "curl_easy_init() failed - returned NULL");
    LOG_EXIT_PTR(MODULE_PHRASE, NULL);
    return NULL;
  }
  LOG_DEBUG(MODULE_PHRASE, "CURL handle created at %p", (void*)curl);

  wb_string s;
  LOG_DEBUG(MODULE_PHRASE, "Initializing response string buffer...");
  init_string(&s);
  LOG_DEBUG(MODULE_PHRASE, "Response buffer initialized: ptr=%p, len=%zu", (void*)s.ptr, s.len);

  LOG_DEBUG(MODULE_PHRASE, "Setting CURL options...");
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
  LOG_DEBUG(MODULE_PHRASE, "CURL options set (follow redirects enabled) " SYM_CHECK);

  LOG_INFO(MODULE_PHRASE, "Performing HTTP request...");
  LOG_NET_OP(MODULE_PHRASE, "curl_easy_perform()");
  res = curl_easy_perform(curl);

  if (res != CURLE_OK) {
    LOG_ERROR(MODULE_PHRASE, "curl_easy_perform() failed: %s", curl_easy_strerror(res));
    LOG_FREE(MODULE_PHRASE, "fetch_phrase", "s.ptr", s.ptr);
    free(s.ptr);
    curl_easy_cleanup(curl);
    LOG_EXIT_PTR(MODULE_PHRASE, NULL);
    return NULL;
  }
  
  LOG_INFO(MODULE_PHRASE, "HTTP request completed successfully " SYM_CHECK);
  LOG_DEBUG(MODULE_PHRASE, "Response length: %zu bytes", s.len);
  LOG_DEBUG(MODULE_PHRASE, "Response preview (first 100 chars): %.100s", s.ptr);

  LOG_DEBUG(MODULE_PHRASE, "Cleaning up CURL handle...");
  curl_easy_cleanup(curl);

  LOG_INFO(MODULE_PHRASE, "Returning response string at %p", (void*)s.ptr);
  LOG_EXIT_PTR(MODULE_PHRASE, s.ptr);
  return s.ptr;
}
