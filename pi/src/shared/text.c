#include "shared/text.h"

#include <stddef.h>
#include <string.h>

#include <glib.h>

#include "shared/log.h"

bool text_is_valid_utf8(const char *text, size_t length) {
    return g_utf8_validate_len(text, length, nullptr);
}

static gchar *fold(const char *text) {
    gchar *composed = g_utf8_normalize(text, -1, G_NORMALIZE_NFC);
    gchar *folded = g_utf8_casefold(composed, -1);
    g_free(composed);
    return folded;
}

bool text_equal_folded(const char *a, const char *b) {
    if (!g_utf8_validate(a, -1, nullptr) || !g_utf8_validate(b, -1, nullptr)) {
        log_error("can't compare text that isn't UTF-8");
        return false;
    }
    gchar *folded_a = fold(a);
    gchar *folded_b = fold(b);
    const bool equal = strcmp(folded_a, folded_b) == 0;
    g_free(folded_a);
    g_free(folded_b);
    return equal;
}
