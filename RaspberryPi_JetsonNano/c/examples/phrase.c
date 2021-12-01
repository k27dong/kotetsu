// #include "EPD_Test.h"

// #define BUFFER_SIZE  (256 * 1024)

// char* phrase_data;
// size_t phrase_len;


// size_t phrase_writer(char *buf, size_t size, size_t items, void* ptr) {
//   phrase_len = phrase_len + (size * items);
//   phrase_data = (char *) realloc(phrase_data, phrase_len);
//   strcat(phrase_data, buf);
//   return size * items;
// }

// char* fetch_word(int temp, int y, int m, int d, int days_since) {
//   CURL* curl;
//   CURLcode res;

//   curl = curl_easy_init();

//   if (!curl) return -1;

//   phrase_len = 10;
//   phrase_data = (char*) malloc(phrase_len);

//   /* construct url */
//   char url[200] = "http://192.168.50.176:5000/api/get_phrase?";
//   char holder[20] = "";

//   sprintf(holder, "temp=%d&", temp);
//   strcat(url, holder);
//   sprintf(holder, "y=%d&", y);
//   strcat(url, holder);
//   sprintf(holder, "m=%d&", m);
//   strcat(url, holder);
//   sprintf(holder, "d=%d&", d);
//   strcat(url, holder);
//   sprintf(holder, "days=%d", days_since);
//   strcat(url, holder);

//   curl_easy_setopt(curl, CURLOPT_URL, url);
//   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, phrase_writer);

//   struct curl_slist *headers = NULL;

//   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
//   res = curl_easy_perform(curl);

//   if (res != CURLE_OK) return -1;

//   char* sanitized_data = strstr(phrase_data, "8FJ20GMV");

//   memmove(&sanitized_data[0], &sanitized_data[8], strlen(sanitized_data) - 0);
//   printf("data: %s\n", sanitized_data);

//   /**
//    * data:
//    * [0]: 'e', 'c'. english / chinese
//    * [1]: 'n', 'w'. line separater: \n or wrap
//    * [2+]: data itself
//    *
//    * cases:
//    * ew: print_en(data)
//    * cw: separate data by 15 chinese characters, multiple print_cn
//    * en/cn: get multiple lines, multiple print
//    */

//   free(phrase_data);
//   curl_easy_cleanup(curl);

//   return sanitized_data;
// }