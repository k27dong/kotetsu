#include "render/text.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <glib.h>

#include "log.h"

bool text_is_valid_utf8(const char *text, size_t length) {
    return g_utf8_validate_len(text, length, nullptr);
}

bool text_is_digit(uint32_t code_point) {
    return g_unichar_isdigit(code_point);
}

bool text_is_han(uint32_t code_point) {
    return g_unichar_get_script(code_point) == G_UNICODE_SCRIPT_HAN;
}

size_t text_digit_runs(const char *text, struct byte_range *out, size_t max) {
    if (!g_utf8_validate(text, -1, nullptr)) {
        log_error("%s: invalid UTF-8", __func__);
        return 0;
    }
    size_t count = 0;
    const char *cursor = text;
    while (*cursor != '\0') {
        if (!text_is_digit(g_utf8_get_char(cursor))) {
            cursor = g_utf8_next_char(cursor);
            continue;
        }
        const char *start = cursor;
        do {
            cursor = g_utf8_next_char(cursor);
        } while (*cursor != '\0' && text_is_digit(g_utf8_get_char(cursor)));
        if (count == max) {
            log_error("%s: more than %zu found; the rest are dropped", __func__, max);
            break;
        }
        out[count++] = (struct byte_range){(size_t)(start - text), (size_t)(cursor - text)};
    }
    return count;
}

size_t text_gap_offsets(const char *text, size_t *out, size_t max) {
    if (!g_utf8_validate(text, -1, nullptr)) {
        log_error("%s: invalid UTF-8", __func__);
        return 0;
    }
    size_t count = 0;
    gunichar previous = 0; /* neither a digit nor Han */
    for (const char *cursor = text; *cursor != '\0'; cursor = g_utf8_next_char(cursor)) {
        const gunichar current = g_utf8_get_char(cursor);
        if ((text_is_digit(previous) && text_is_han(current)) ||
            (text_is_han(previous) && text_is_digit(current))) {
            if (count == max) {
                log_error("%s: more than %zu found; the rest are dropped", __func__, max);
                break;
            }
            out[count++] = (size_t)(cursor - text);
        }
        previous = current;
    }
    return count;
}

static gchar *fold(const char *text) {
    gchar *normalized = g_utf8_normalize(text, -1, G_NORMALIZE_NFC);
    gchar *folded = g_utf8_casefold(normalized, -1);
    g_free(normalized);
    return folded;
}

bool text_equal_folded(const char *a, const char *b) {
    if (!g_utf8_validate(a, -1, nullptr) || !g_utf8_validate(b, -1, nullptr)) {
        log_error("%s: invalid UTF-8", __func__);
        return false;
    }
    gchar *folded_a = fold(a);
    gchar *folded_b = fold(b);
    const bool equal = strcmp(folded_a, folded_b) == 0;
    g_free(folded_a);
    g_free(folded_b);
    return equal;
}
