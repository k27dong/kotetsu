#pragma once

#include <stddef.h>
#include <time.h>

#include "shared/http.h"

#define WARNINGS_URL "https://danepubliczne.imgw.pl/api/data/warningsmeteo"

static constexpr size_t WARNING_LABEL_BYTES = 32;

struct warnings {
    bool ok;
    bool any;
    char label[WARNING_LABEL_BYTES]; // the top one in force: highest level, then latest published
};

struct warnings warnings_fetch(time_t now);
// {"message": …} means none; any other non-list is an error
[[nodiscard]] struct warnings warnings_read(const struct http_response *response, time_t now);
