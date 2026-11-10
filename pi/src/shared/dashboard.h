#pragma once

#include "shared/http.h"

enum dashboard_status {
    DASHBOARD_OK,
    DASHBOARD_UNREACHABLE,
    DASHBOARD_UNAUTHORIZED,
    DASHBOARD_BAD_RESPONSE,
    DASHBOARD_NO_PHRASE,
};

// logs an unexpected answer with its error field, and names its problem
[[nodiscard]] enum dashboard_status dashboard_log_answer(const char *url,
                                                         const struct http_response *response);
// nullptr for DASHBOARD_OK
const char *dashboard_problem(enum dashboard_status status);
