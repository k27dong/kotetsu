#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"

int main(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("kotetsu " KOTETSU_VERSION);
        return EXIT_SUCCESS;
    }
    log_error("usage: kotetsu --version");
    return EXIT_FAILURE;
}
