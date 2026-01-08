/******************************************************************************
 * | File      : logger.c
 * | Author    : kotetsu
 * | Function  : Comprehensive logging system implementation
 * | Info      : Provides visually appealing debug output for tracing execution
 ******************************************************************************/
#include "logger.h"

/*============================================================================
 * Static Variables
 *============================================================================*/
static LogLevel g_log_level = LOG_LEVEL_DEBUG;
static int g_initialized = 0;
static struct timeval g_start_time;

/*============================================================================
 * Level Configuration
 *============================================================================*/
typedef struct {
    const char *name;
    const char *symbol;
    const char *color;
    const char *bg_color;
} LevelConfig;

static const LevelConfig level_configs[] = {
    [LOG_LEVEL_DEBUG] = {"DEBUG", SYM_DEBUG, ANSI_CYAN,         ""},
    [LOG_LEVEL_INFO]  = {"INFO",  SYM_INFO,  ANSI_BRIGHT_GREEN, ""},
    [LOG_LEVEL_WARN]  = {"WARN",  SYM_WARN,  ANSI_BRIGHT_YELLOW,""},
    [LOG_LEVEL_ERROR] = {"ERROR", SYM_ERROR, ANSI_BRIGHT_RED,   ""},
    [LOG_LEVEL_FATAL] = {"FATAL", SYM_FATAL, ANSI_WHITE,        ANSI_BG_RED}
};

/*============================================================================
 * Module Color Configuration
 *============================================================================*/
static const char* get_module_color(const char *module) {
    if (strcmp(module, MODULE_MAIN) == 0)     return ANSI_BRIGHT_WHITE;
    if (strcmp(module, MODULE_DISPLAY) == 0)  return ANSI_BRIGHT_CYAN;
    if (strcmp(module, MODULE_WEATHER) == 0)  return ANSI_BRIGHT_BLUE;
    if (strcmp(module, MODULE_PHRASE) == 0)   return ANSI_BRIGHT_MAGENTA;
    if (strcmp(module, MODULE_DATE) == 0)     return ANSI_BRIGHT_YELLOW;
    if (strcmp(module, MODULE_HELPER) == 0)   return ANSI_GREEN;
    if (strcmp(module, MODULE_EPD) == 0)      return ANSI_MAGENTA;
    if (strcmp(module, MODULE_GPIO) == 0)     return ANSI_YELLOW;
    if (strcmp(module, MODULE_SPI) == 0)      return ANSI_BLUE;
    if (strcmp(module, MODULE_CONFIG) == 0)   return ANSI_CYAN;
    return ANSI_WHITE;
}

/*============================================================================
 * Timestamp Functions
 *============================================================================*/
static void get_timestamp(char *buffer, size_t size) {
    struct timeval tv;
    struct tm *tm_info;
    
    gettimeofday(&tv, NULL);
    tm_info = localtime(&tv.tv_sec);
    
    snprintf(buffer, size, "%02d:%02d:%02d.%03ld",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             tv.tv_usec / 1000);
}

static long get_elapsed_ms(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    
    long seconds = now.tv_sec - g_start_time.tv_sec;
    long useconds = now.tv_usec - g_start_time.tv_usec;
    
    return (seconds * 1000) + (useconds / 1000);
}

/*============================================================================
 * Public Functions
 *============================================================================*/

void logger_init(void) {
    if (g_initialized) return;
    
    gettimeofday(&g_start_time, NULL);
    g_initialized = 1;
    
    /* Check for LOG_LEVEL environment variable */
    const char *env_level = getenv("KOTETSU_LOG_LEVEL");
    if (env_level != NULL) {
        if (strcmp(env_level, "DEBUG") == 0) g_log_level = LOG_LEVEL_DEBUG;
        else if (strcmp(env_level, "INFO") == 0) g_log_level = LOG_LEVEL_INFO;
        else if (strcmp(env_level, "WARN") == 0) g_log_level = LOG_LEVEL_WARN;
        else if (strcmp(env_level, "ERROR") == 0) g_log_level = LOG_LEVEL_ERROR;
        else if (strcmp(env_level, "FATAL") == 0) g_log_level = LOG_LEVEL_FATAL;
    }
}

void logger_set_level(LogLevel level) {
    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_FATAL) {
        g_log_level = level;
    }
}

LogLevel logger_get_level(void) {
    return g_log_level;
}

void logger_log(LogLevel level, const char *module, const char *func, 
                int line, const char *fmt, ...) {
    if (level < g_log_level) return;
    if (!g_initialized) logger_init();
    
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    
    const LevelConfig *cfg = &level_configs[level];
    const char *mod_color = get_module_color(module);
    
    /* Print timestamp */
    fprintf(stderr, "%s[%s]%s ", ANSI_DIM, timestamp, ANSI_RESET);
    
    /* Print elapsed time */
    fprintf(stderr, "%s[+%6ldms]%s ", ANSI_DIM, get_elapsed_ms(), ANSI_RESET);
    
    /* Print level with color and symbol */
    fprintf(stderr, "%s%s%s%-5s%s ", 
            cfg->bg_color, cfg->color, ANSI_BOLD, cfg->name, ANSI_RESET);
    fprintf(stderr, "%s ", cfg->symbol);
    
    /* Print module */
    fprintf(stderr, "%s%s[%-8s]%s ", mod_color, ANSI_BOLD, module, ANSI_RESET);
    
    /* Print function and line */
    fprintf(stderr, "%s%s:%d%s " SYM_ARROW " ", 
            ANSI_DIM, func, line, ANSI_RESET);
    
    /* Print the actual message */
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    fprintf(stderr, "\n");
    fflush(stderr);
}

void logger_func_enter(const char *module, const char *func) {
    if (LOG_LEVEL_DEBUG < g_log_level) return;
    if (!g_initialized) logger_init();
    
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    
    const char *mod_color = get_module_color(module);
    
    fprintf(stderr, "%s[%s]%s ", ANSI_DIM, timestamp, ANSI_RESET);
    fprintf(stderr, "%s[+%6ldms]%s ", ANSI_DIM, get_elapsed_ms(), ANSI_RESET);
    fprintf(stderr, "%s%sTRACE%s ", ANSI_MAGENTA, ANSI_BOLD, ANSI_RESET);
    fprintf(stderr, "%s ", SYM_ENTER);
    fprintf(stderr, "%s%s[%-8s]%s ", mod_color, ANSI_BOLD, module, ANSI_RESET);
    fprintf(stderr, "%sEntering%s %s%s%s()\n", 
            ANSI_DIM, ANSI_RESET, ANSI_BRIGHT_WHITE, func, ANSI_RESET);
    fflush(stderr);
}

void logger_func_exit(const char *module, const char *func, int return_value) {
    if (LOG_LEVEL_DEBUG < g_log_level) return;
    if (!g_initialized) logger_init();
    
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    
    const char *mod_color = get_module_color(module);
    const char *ret_color = (return_value == 0) ? ANSI_GREEN : ANSI_YELLOW;
    
    fprintf(stderr, "%s[%s]%s ", ANSI_DIM, timestamp, ANSI_RESET);
    fprintf(stderr, "%s[+%6ldms]%s ", ANSI_DIM, get_elapsed_ms(), ANSI_RESET);
    fprintf(stderr, "%s%sTRACE%s ", ANSI_MAGENTA, ANSI_BOLD, ANSI_RESET);
    fprintf(stderr, "%s ", SYM_EXIT);
    fprintf(stderr, "%s%s[%-8s]%s ", mod_color, ANSI_BOLD, module, ANSI_RESET);
    fprintf(stderr, "%sExiting%s %s%s%s() %s" SYM_ARROW " ret=%s%d%s\n", 
            ANSI_DIM, ANSI_RESET, ANSI_BRIGHT_WHITE, func, ANSI_RESET,
            ANSI_DIM, ret_color, return_value, ANSI_RESET);
    fflush(stderr);
}

void logger_func_exit_ptr(const char *module, const char *func, const void *ptr) {
    if (LOG_LEVEL_DEBUG < g_log_level) return;
    if (!g_initialized) logger_init();
    
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    
    const char *mod_color = get_module_color(module);
    const char *ret_color = (ptr != NULL) ? ANSI_GREEN : ANSI_RED;
    const char *ret_sym = (ptr != NULL) ? SYM_CHECK : SYM_CROSS;
    
    fprintf(stderr, "%s[%s]%s ", ANSI_DIM, timestamp, ANSI_RESET);
    fprintf(stderr, "%s[+%6ldms]%s ", ANSI_DIM, get_elapsed_ms(), ANSI_RESET);
    fprintf(stderr, "%s%sTRACE%s ", ANSI_MAGENTA, ANSI_BOLD, ANSI_RESET);
    fprintf(stderr, "%s ", SYM_EXIT);
    fprintf(stderr, "%s%s[%-8s]%s ", mod_color, ANSI_BOLD, module, ANSI_RESET);
    fprintf(stderr, "%sExiting%s %s%s%s() %s" SYM_ARROW " ptr=%s%p %s%s\n", 
            ANSI_DIM, ANSI_RESET, ANSI_BRIGHT_WHITE, func, ANSI_RESET,
            ANSI_DIM, ret_color, ptr, ret_sym, ANSI_RESET);
    fflush(stderr);
}

void logger_section_start(const char *title) {
    if (!g_initialized) logger_init();
    
    int title_len = strlen(title);
    int total_width = 60;
    int padding = (total_width - title_len - 4) / 2;
    
    fprintf(stderr, "\n%s%s", ANSI_BRIGHT_CYAN, BOX_TOP_LEFT);
    for (int i = 0; i < total_width - 2; i++) fprintf(stderr, "%s", BOX_HORIZONTAL);
    fprintf(stderr, "%s%s\n", BOX_TOP_RIGHT, ANSI_RESET);
    
    fprintf(stderr, "%s%s%s", ANSI_BRIGHT_CYAN, BOX_VERTICAL, ANSI_RESET);
    for (int i = 0; i < padding; i++) fprintf(stderr, " ");
    fprintf(stderr, "%s%s %s %s", ANSI_BOLD, ANSI_BRIGHT_WHITE, title, ANSI_RESET);
    for (int i = 0; i < total_width - title_len - padding - 4; i++) fprintf(stderr, " ");
    fprintf(stderr, "%s%s%s\n", ANSI_BRIGHT_CYAN, BOX_VERTICAL, ANSI_RESET);
    
    fprintf(stderr, "%s%s", ANSI_BRIGHT_CYAN, BOX_BOTTOM_LEFT);
    for (int i = 0; i < total_width - 2; i++) fprintf(stderr, "%s", BOX_HORIZONTAL);
    fprintf(stderr, "%s%s\n\n", BOX_BOTTOM_RIGHT, ANSI_RESET);
    
    fflush(stderr);
}

void logger_section_end(const char *title) {
    if (!g_initialized) logger_init();
    
    fprintf(stderr, "\n%s%s", ANSI_DIM, BOX_TOP_LEFT);
    for (int i = 0; i < 58; i++) fprintf(stderr, "%s", BOX_HORIZONTAL);
    fprintf(stderr, "%s%s\n", BOX_TOP_RIGHT, ANSI_RESET);
    
    fprintf(stderr, "%s%s  %s End: %s %s%s\n", 
            ANSI_DIM, BOX_VERTICAL, SYM_CHECK, title, BOX_VERTICAL, ANSI_RESET);
    
    fprintf(stderr, "%s%s", ANSI_DIM, BOX_BOTTOM_LEFT);
    for (int i = 0; i < 58; i++) fprintf(stderr, "%s", BOX_HORIZONTAL);
    fprintf(stderr, "%s%s\n\n", BOX_BOTTOM_RIGHT, ANSI_RESET);
    
    fflush(stderr);
}

void logger_separator(void) {
    if (!g_initialized) logger_init();
    
    fprintf(stderr, "%s", ANSI_DIM);
    for (int i = 0; i < 70; i++) fprintf(stderr, "%s", BOX_HORIZONTAL);
    fprintf(stderr, "%s\n", ANSI_RESET);
    fflush(stderr);
}

void logger_check_ptr(const char *module, const char *func, const char *name, 
                      const void *ptr, int line) {
    if (LOG_LEVEL_DEBUG < g_log_level) return;
    if (!g_initialized) logger_init();
    
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    
    const char *mod_color = get_module_color(module);
    const char *status_color = (ptr != NULL) ? ANSI_GREEN : ANSI_BRIGHT_RED;
    const char *status_sym = (ptr != NULL) ? SYM_CHECK : SYM_CROSS;
    
    fprintf(stderr, "%s[%s]%s ", ANSI_DIM, timestamp, ANSI_RESET);
    fprintf(stderr, "%s[+%6ldms]%s ", ANSI_DIM, get_elapsed_ms(), ANSI_RESET);
    fprintf(stderr, "%s%sPTR  %s ", ANSI_BLUE, ANSI_BOLD, ANSI_RESET);
    fprintf(stderr, "%s ", SYM_MEMORY);
    fprintf(stderr, "%s%s[%-8s]%s ", mod_color, ANSI_BOLD, module, ANSI_RESET);
    fprintf(stderr, "%s%s:%d%s " SYM_ARROW " ", ANSI_DIM, func, line, ANSI_RESET);
    fprintf(stderr, "%s = %s%p %s%s\n", 
            name, status_color, ptr, status_sym, ANSI_RESET);
    fflush(stderr);
}

void logger_malloc(const char *module, const char *func, const char *name, 
                   void *ptr, size_t size) {
    if (LOG_LEVEL_DEBUG < g_log_level) return;
    if (!g_initialized) logger_init();
    
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    
    const char *mod_color = get_module_color(module);
    const char *status = (ptr != NULL) ? SYM_CHECK : SYM_CROSS;
    const char *color = (ptr != NULL) ? ANSI_GREEN : ANSI_RED;
    
    fprintf(stderr, "%s[%s]%s ", ANSI_DIM, timestamp, ANSI_RESET);
    fprintf(stderr, "%s[+%6ldms]%s ", ANSI_DIM, get_elapsed_ms(), ANSI_RESET);
    fprintf(stderr, "%s%sALLOC%s ", ANSI_YELLOW, ANSI_BOLD, ANSI_RESET);
    fprintf(stderr, "%s ", SYM_MEMORY);
    fprintf(stderr, "%s%s[%-8s]%s ", mod_color, ANSI_BOLD, module, ANSI_RESET);
    fprintf(stderr, "%smalloc%s(%zu bytes) for '%s' %s" SYM_ARROW " %s%p %s%s\n",
            ANSI_CYAN, ANSI_RESET, size, name, ANSI_DIM, color, ptr, status, ANSI_RESET);
    fflush(stderr);
}

void logger_free(const char *module, const char *func, const char *name, void *ptr) {
    if (LOG_LEVEL_DEBUG < g_log_level) return;
    if (!g_initialized) logger_init();
    
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    
    const char *mod_color = get_module_color(module);
    
    fprintf(stderr, "%s[%s]%s ", ANSI_DIM, timestamp, ANSI_RESET);
    fprintf(stderr, "%s[+%6ldms]%s ", ANSI_DIM, get_elapsed_ms(), ANSI_RESET);
    fprintf(stderr, "%s%sFREE %s ", ANSI_YELLOW, ANSI_BOLD, ANSI_RESET);
    fprintf(stderr, "%s ", SYM_MEMORY);
    fprintf(stderr, "%s%s[%-8s]%s ", mod_color, ANSI_BOLD, module, ANSI_RESET);
    fprintf(stderr, "%sfree%s '%s' at %s%p%s\n",
            ANSI_CYAN, ANSI_RESET, name, ANSI_DIM, ptr, ANSI_RESET);
    fflush(stderr);
}

void logger_banner(const char *app_name, const char *version) {
    if (!g_initialized) logger_init();
    
    fprintf(stderr, "\n");
    fprintf(stderr, "%s%s", ANSI_BRIGHT_CYAN, ANSI_BOLD);
    fprintf(stderr, "  ╔═══════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "  ║                                                           ║\n");
    fprintf(stderr, "  ║   %s█▄▀ █▀█ ▀█▀ █▀▀ ▀█▀ █▀ █ █%s                             ║\n", 
            ANSI_BRIGHT_WHITE, ANSI_BRIGHT_CYAN);
    fprintf(stderr, "  ║   %s█░█ █▄█ ░█░ ██▄ ░█░ ▄█ █▄█%s                             ║\n",
            ANSI_BRIGHT_WHITE, ANSI_BRIGHT_CYAN);
    fprintf(stderr, "  ║                                                           ║\n");
    fprintf(stderr, "  ║   %sE-Paper Display Controller%s                              ║\n",
            ANSI_WHITE, ANSI_BRIGHT_CYAN);
    fprintf(stderr, "  ║   %sVersion: %s%s                                             ║\n",
            ANSI_DIM, version, ANSI_BRIGHT_CYAN);
    fprintf(stderr, "  ║                                                           ║\n");
    fprintf(stderr, "  ╚═══════════════════════════════════════════════════════════╝\n");
    fprintf(stderr, "%s\n", ANSI_RESET);
    
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    fprintf(stderr, "  %s%sStarted at: %s%s\n\n", ANSI_DIM, SYM_BULLET, timestamp, ANSI_RESET);
    
    fflush(stderr);
}
