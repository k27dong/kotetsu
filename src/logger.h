/******************************************************************************
 * | File      : logger.h
 * | Author    : kotetsu
 * | Function  : Comprehensive logging system with colors, timestamps, and levels
 * | Info      : Provides visually appealing debug output for tracing execution
 ******************************************************************************/
#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>

/*============================================================================
 * Log Levels
 *============================================================================*/
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_FATAL = 4
} LogLevel;

/*============================================================================
 * ANSI Color Codes
 *============================================================================*/
#define ANSI_RESET       "\033[0m"
#define ANSI_BOLD        "\033[1m"
#define ANSI_DIM         "\033[2m"

/* Foreground colors */
#define ANSI_BLACK       "\033[30m"
#define ANSI_RED         "\033[31m"
#define ANSI_GREEN       "\033[32m"
#define ANSI_YELLOW      "\033[33m"
#define ANSI_BLUE        "\033[34m"
#define ANSI_MAGENTA     "\033[35m"
#define ANSI_CYAN        "\033[36m"
#define ANSI_WHITE       "\033[37m"

/* Bright foreground colors */
#define ANSI_BRIGHT_RED     "\033[91m"
#define ANSI_BRIGHT_GREEN   "\033[92m"
#define ANSI_BRIGHT_YELLOW  "\033[93m"
#define ANSI_BRIGHT_BLUE    "\033[94m"
#define ANSI_BRIGHT_MAGENTA "\033[95m"
#define ANSI_BRIGHT_CYAN    "\033[96m"
#define ANSI_BRIGHT_WHITE   "\033[97m"

/* Background colors */
#define ANSI_BG_RED      "\033[41m"
#define ANSI_BG_GREEN    "\033[42m"
#define ANSI_BG_YELLOW   "\033[43m"
#define ANSI_BG_BLUE     "\033[44m"
#define ANSI_BG_MAGENTA  "\033[45m"
#define ANSI_BG_WHITE    "\033[47m"

/*============================================================================
 * Visual Symbols
 *============================================================================*/
#define SYM_DEBUG    "🔍"
#define SYM_INFO     "ℹ️ "
#define SYM_WARN     "⚠️ "
#define SYM_ERROR    "❌"
#define SYM_FATAL    "💀"
#define SYM_ENTER    "▶"
#define SYM_EXIT     "◀"
#define SYM_ARROW    "→"
#define SYM_CHECK    "✓"
#define SYM_CROSS    "✗"
#define SYM_BULLET   "•"
#define SYM_HARDWARE "🔧"
#define SYM_NETWORK  "🌐"
#define SYM_MEMORY   "📦"
#define SYM_DISPLAY  "🖥️ "

/*============================================================================
 * Box Drawing Characters
 *============================================================================*/
#define BOX_HORIZONTAL   "─"
#define BOX_VERTICAL     "│"
#define BOX_TOP_LEFT     "┌"
#define BOX_TOP_RIGHT    "┐"
#define BOX_BOTTOM_LEFT  "└"
#define BOX_BOTTOM_RIGHT "┘"
#define BOX_T_DOWN       "┬"
#define BOX_T_UP         "┴"
#define BOX_T_RIGHT      "├"
#define BOX_T_LEFT       "┤"
#define BOX_CROSS        "┼"

/*============================================================================
 * Module Identifiers
 *============================================================================*/
#define MODULE_MAIN      "MAIN"
#define MODULE_DISPLAY   "DISPLAY"
#define MODULE_WEATHER   "WEATHER"
#define MODULE_PHRASE    "PHRASE"
#define MODULE_DATE      "DATE"
#define MODULE_HELPER    "HELPER"
#define MODULE_EPD       "EPD"
#define MODULE_GPIO      "GPIO"
#define MODULE_SPI       "SPI"
#define MODULE_CONFIG    "CONFIG"

/*============================================================================
 * Function Declarations
 *============================================================================*/

/* Initialize the logger (call once at program start) */
void logger_init(void);

/* Set the minimum log level (default: LOG_LEVEL_DEBUG) */
void logger_set_level(LogLevel level);

/* Get current log level */
LogLevel logger_get_level(void);

/* Core logging function */
void logger_log(LogLevel level, const char *module, const char *func, 
                int line, const char *fmt, ...);

/* Function entry/exit logging */
void logger_func_enter(const char *module, const char *func);
void logger_func_exit(const char *module, const char *func, int return_value);
void logger_func_exit_ptr(const char *module, const char *func, const void *ptr);

/* Section headers for visual organization */
void logger_section_start(const char *title);
void logger_section_end(const char *title);

/* Separator line */
void logger_separator(void);

/* Pointer validation logging */
void logger_check_ptr(const char *module, const char *func, const char *name, 
                      const void *ptr, int line);

/* Memory operation logging */
void logger_malloc(const char *module, const char *func, const char *name, 
                   void *ptr, size_t size);
void logger_free(const char *module, const char *func, const char *name, void *ptr);

/* Print startup banner */
void logger_banner(const char *app_name, const char *version);

/*============================================================================
 * Convenience Macros
 *============================================================================*/

/* Main logging macros with automatic file/line info */
#define LOG_DEBUG(module, fmt, ...) \
    logger_log(LOG_LEVEL_DEBUG, module, __func__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(module, fmt, ...) \
    logger_log(LOG_LEVEL_INFO, module, __func__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(module, fmt, ...) \
    logger_log(LOG_LEVEL_WARN, module, __func__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(module, fmt, ...) \
    logger_log(LOG_LEVEL_ERROR, module, __func__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_FATAL(module, fmt, ...) \
    logger_log(LOG_LEVEL_FATAL, module, __func__, __LINE__, fmt, ##__VA_ARGS__)

/* Function tracing macros */
#define LOG_ENTER(module) \
    logger_func_enter(module, __func__)

#define LOG_EXIT(module, ret) \
    logger_func_exit(module, __func__, ret)

#define LOG_EXIT_PTR(module, ptr) \
    logger_func_exit_ptr(module, __func__, ptr)

/* Pointer check macro - logs and returns if NULL */
#define LOG_CHECK_PTR(module, name, ptr) \
    do { \
        logger_check_ptr(module, __func__, name, ptr, __LINE__); \
        if ((ptr) == NULL) { \
            LOG_ERROR(module, "NULL pointer detected: %s", name); \
            return; \
        } \
    } while(0)

#define LOG_CHECK_PTR_RET(module, name, ptr, retval) \
    do { \
        logger_check_ptr(module, __func__, name, ptr, __LINE__); \
        if ((ptr) == NULL) { \
            LOG_ERROR(module, "NULL pointer detected: %s", name); \
            return retval; \
        } \
    } while(0)

/* Memory operation macros */
#define LOG_MALLOC(module, name, ptr, size) \
    logger_malloc(module, __func__, name, ptr, size)

#define LOG_FREE(module, name, ptr) \
    logger_free(module, __func__, name, ptr)

/* Hardware operation logging */
#define LOG_HW_OP(module, fmt, ...) \
    logger_log(LOG_LEVEL_DEBUG, module, __func__, __LINE__, \
               SYM_HARDWARE " " fmt, ##__VA_ARGS__)

/* Network operation logging */
#define LOG_NET_OP(module, fmt, ...) \
    logger_log(LOG_LEVEL_DEBUG, module, __func__, __LINE__, \
               SYM_NETWORK " " fmt, ##__VA_ARGS__)

#endif /* _LOGGER_H_ */
