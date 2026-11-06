#pragma once

#include "shared/http.h"

enum dashboard_status {
    DASHBOARD_OK,
    DASHBOARD_UNREACHABLE,
    DASHBOARD_UNAUTHORIZED,
    DASHBOARD_BAD_RESPONSE,
    DASHBOARD_NO_PHRASE,
};

// for an answer other than the one hoped for: logs it with its error field, and names the problem
enum dashboard_status dashboard_log_answer(const char *url, const struct http_response *response);
const char *dashboard_problem(enum dashboard_status status); // nullptr for DASHBOARD_OK
