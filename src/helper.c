#include "basic.h"
#include "logger.h"

void init_string(wb_string *s) {
  LOG_ENTER(MODULE_HELPER);
  
  if (s == NULL) {
    LOG_ERROR(MODULE_HELPER, "wb_string pointer is NULL!");
    return;
  }
  
  s->len = 0;
  LOG_DEBUG(MODULE_HELPER, "Allocating initial buffer (1 byte)...");
  s->ptr = malloc(s->len + 1);
  LOG_MALLOC(MODULE_HELPER, "s->ptr", s->ptr, s->len + 1);

  if (s->ptr == NULL) {
    LOG_FATAL(MODULE_HELPER, "malloc() failed for initial string buffer!");
    fprintf(stderr, "malloc() failed\n");
    exit(EXIT_FAILURE);
  }

  s->ptr[0] = '\0';
  LOG_DEBUG(MODULE_HELPER, "String initialized: ptr=%p, len=%zu", (void*)s->ptr, s->len);
  LOG_EXIT(MODULE_HELPER, 0);
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, wb_string *s) {
  /* Note: This function is called frequently by CURL, so we use minimal logging
     to avoid performance impact. Only log errors. */
  
  if (ptr == NULL || s == NULL) {
    LOG_ERROR(MODULE_HELPER, "writefunc called with NULL pointer: ptr=%p, s=%p",
              ptr, (void*)s);
    return 0;
  }
  
  size_t new_len = s->len + size * nmemb;
  size_t old_len = s->len;
  
  s->ptr = realloc(s->ptr, new_len + 1);

  if (s->ptr == NULL) {
    LOG_FATAL(MODULE_HELPER, "realloc() failed! Requested %zu bytes", new_len + 1);
    fprintf(stderr, "realloc() failed\n");
    exit(EXIT_FAILURE);
  }

  memcpy(s->ptr + s->len, ptr, size * nmemb);
  s->ptr[new_len] = '\0';
  s->len = new_len;

  /* Only log on significant data chunks to reduce noise */
  if (size * nmemb > 100) {
    LOG_DEBUG(MODULE_HELPER, "writefunc: received %zu bytes (total: %zu)", 
              size * nmemb, new_len);
  }

  return size * nmemb;
}
