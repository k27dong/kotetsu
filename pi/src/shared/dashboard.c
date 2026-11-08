#include "shared/dashboard.h"

#include <stdlib.h>

#include <cjson/cJSON.h>

#include "shared/http.h"
#include "shared/json.h"
#include "shared/log.h"

enum dashboard_status dashboard_log_answer(const char *url, const struct http_response *response) {
    /* the dashboard says why in an error field, when the answer is its own */
    cJSON *root = cJSON_Parse(response->body);
    const char *error = cJSON_GetStringValue(json_member(root, "error"));
    log_error("%s answered %ld%s%s", url, response->status, error != nullptr ? ": " : "",
              error != nullptr ? error : "");
    cJSON_Delete(root);

    /* what the status means */
    if (response->status == 401 || response->status == 403) {
        return DASHBOARD_UNAUTHORIZED;
    }
    if (response->status >= 500) {
        return DASHBOARD_UNREACHABLE;
    }
    return DASHBOARD_BAD_RESPONSE;
}

const char *dashboard_problem(enum dashboard_status status) {
    switch (status) {
    case DASHBOARD_OK:
        return nullptr;
    case DASHBOARD_UNREACHABLE:
        return "dashboard unreachable";
    case DASHBOARD_UNAUTHORIZED:
        return "dashboard unauthorized";
    case DASHBOARD_BAD_RESPONSE:
        return "dashboard bad response";
    case DASHBOARD_NO_PHRASE:
        return "no phrase available";
    }
    log_error("%d isn't a dashboard status", (int)status);
    abort();
}
