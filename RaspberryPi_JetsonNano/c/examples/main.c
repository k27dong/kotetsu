#include <stdlib.h>     //exit()
#include <signal.h>     //signal()
#include "EPD_Test.h"   //Examples

void Handler (int signo) {
  //System Exit
  printf("\r\nHandler:exit\r\n");
  DEV_Module_Exit();

  exit(0);
}

int main(void) {
  signal(SIGINT, Handler);

  EPD_2in7_test();

  // if(DEV_Module_Init()!=0){
  //   return -1;
  // }
	// while(1) {
  //   EPD_7in5b_V2_test();
	// 	DEV_Delay_ms(10000);
	// }
	// DEV_Module_Exit();


  return 0;
}
