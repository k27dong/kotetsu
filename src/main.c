#pragma GCC diagnostic ignored "-Wuninitialized"

#include <signal.h>
#include <stdlib.h>
#include <time.h>

#include "EPD_2in7.h"
#include "basic.h"
#include "logger.h"

#define APP_VERSION "1.0.0"

void sigint_handler(int signo) {
  LOG_WARN(MODULE_MAIN, "Received SIGINT (signal %d) - initiating graceful shutdown", signo);
  logger_section_start("SIGNAL HANDLER: SIGINT");
  
  LOG_INFO(MODULE_MAIN, "Calling DEV_Module_Exit()...");
  DEV_Module_Exit();
  LOG_INFO(MODULE_MAIN, "Module exit complete");
  
  logger_section_end("SIGNAL HANDLER");
  printf("\r\nHandler:exit\r\n");
  exit(0);
}

void err_handler(int signo) {
  LOG_ERROR(MODULE_MAIN, "Error handler triggered (signal %d)", signo);
  logger_section_start("ERROR HANDLER");
  
  LOG_INFO(MODULE_MAIN, "Attempting screen shutdown...");
  screen_shutdown();
  LOG_INFO(MODULE_MAIN, "Screen shutdown complete");
  
  logger_section_end("ERROR HANDLER");
  exit(0);
}

void cleanup(UBYTE *img) {
  LOG_ENTER(MODULE_MAIN);
  
  if (img != NULL) {
    LOG_FREE(MODULE_MAIN, "cleanup", "image", img);
    free(img);
    img = NULL;
    LOG_DEBUG(MODULE_MAIN, "Image memory freed successfully");
  } else {
    LOG_WARN(MODULE_MAIN, "cleanup called with NULL image pointer");
  }
  
  LOG_EXIT(MODULE_MAIN, 0);
}

int main(void) {
  /* Initialize logger first */
  logger_init();
  logger_banner("KOTETSU", APP_VERSION);
  
  logger_section_start("INITIALIZATION");
  
  LOG_INFO(MODULE_MAIN, "Setting up signal handlers...");
  signal(SIGINT, sigint_handler);
  signal(SIGSEGV, err_handler);
  LOG_DEBUG(MODULE_MAIN, "SIGINT handler: sigint_handler()");
  LOG_DEBUG(MODULE_MAIN, "SIGSEGV handler: err_handler()");
  LOG_INFO(MODULE_MAIN, "Signal handlers configured " SYM_CHECK);
  
  logger_separator();
  
  LOG_INFO(MODULE_MAIN, "Initializing e-paper screen...");
  int init_result = screen_init();
  if (init_result != 0) {
    LOG_FATAL(MODULE_MAIN, "Screen initialization failed with code: %d", init_result);
    return -1;
  }
  LOG_INFO(MODULE_MAIN, "Screen initialized successfully " SYM_CHECK);
  
  logger_section_end("INITIALIZATION");
  
  logger_section_start("IMAGE GENERATION");
  
  UBYTE *image = NULL;
  LOG_DEBUG(MODULE_MAIN, "Image pointer initialized to NULL (expected)");
  
  LOG_INFO(MODULE_MAIN, "Calling gen_image()...");
  int gen_result = gen_image(&image);  /* Pass pointer to pointer */
  
  LOG_DEBUG(MODULE_MAIN, "gen_image returned: %d", gen_result);
  logger_check_ptr(MODULE_MAIN, "main", "image (after gen)", image, __LINE__);
  
  if (image == NULL) {
    LOG_ERROR(MODULE_MAIN, "Image generation failed - image pointer is still NULL!");
  } else {
    LOG_INFO(MODULE_MAIN, "Image generated successfully at address %p", (void*)image);
  }
  
  logger_section_end("IMAGE GENERATION");
  
  logger_section_start("CLEANUP");
  
  LOG_INFO(MODULE_MAIN, "Cleaning up resources...");
  cleanup(image);
  LOG_INFO(MODULE_MAIN, "Image cleanup complete " SYM_CHECK);
  
  LOG_INFO(MODULE_MAIN, "Shutting down screen...");
  screen_shutdown();
  LOG_INFO(MODULE_MAIN, "Screen shutdown complete " SYM_CHECK);
  
  logger_section_end("CLEANUP");
  
  LOG_INFO(MODULE_MAIN, "Program completed successfully!");
  logger_separator();
  
  return 0;
}
