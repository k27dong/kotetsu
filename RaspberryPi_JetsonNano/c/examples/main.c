﻿#include <stdlib.h>     //exit()
#include <signal.h>     //signal()
#include <time.h>
#include "EPD_Test.h"
#include "EPD_2in7.h"

void sigint_handler(int signo) {
  //System Exit
  printf("\r\nHandler:exit\r\n");
  DEV_Module_Exit();

  exit(0);
}

void err_handler(int signo) {
  screen_shutdown();
  exit(0);
}

void cleanup(UBYTE* img) {
  free(img);
  img = NULL;
}

int main(void) {
  signal(SIGINT, sigint_handler);

  screen_init();

  UBYTE* image;

  gen_image(image);

  cleanup(image);

  screen_shutdown();

  return 0;
}
