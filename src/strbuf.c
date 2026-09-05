#include "strbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void nixie_strbuf_init(nixie_strbuf_t *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int strbuf_reserve(nixie_strbuf_t *sb, size_t extra) {
    size_t needed = sb->len + extra + 1; /* +1 for NUL */
    if (needed <= sb->cap) {
        return 1;
    }

    size_t new_cap = sb->cap == 0 ? 256 : sb->cap;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    char *new_data = (char *)realloc(sb->data, new_cap);
    if (new_data == NULL) {
        return 0;
    }

    sb->data = new_data;
    sb->cap = new_cap;
    return 1;
}

int nixie_strbuf_append_n(nixie_strbuf_t *sb, const char *text, size_t n) {
    if (n == 0) {
        if (sb->data == NULL && !strbuf_reserve(sb, 0)) {
            return 0;
        }
        sb->data[sb->len] = '\0';
        return 1;
    }

    if (!strbuf_reserve(sb, n)) {
        return 0;
    }

    memcpy(sb->data + sb->len, text, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 1;
}

int nixie_strbuf_append(nixie_strbuf_t *sb, const char *text) {
    if (text == NULL) {
        return 1;
    }
    return nixie_strbuf_append_n(sb, text, strlen(text));
}

int nixie_strbuf_append_char(nixie_strbuf_t *sb, char c) {
    return nixie_strbuf_append_n(sb, &c, 1);
}

int nixie_strbuf_appendf(nixie_strbuf_t *sb, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);

    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (needed < 0) {
        va_end(args_copy);
        return 0;
    }

    if (!strbuf_reserve(sb, (size_t)needed)) {
        va_end(args_copy);
        return 0;
    }

    vsnprintf(sb->data + sb->len, (size_t)needed + 1, fmt, args_copy);
    va_end(args_copy);

    sb->len += (size_t)needed;
    return 1;
}

char *nixie_strbuf_release(nixie_strbuf_t *sb) {
    if (sb->data == NULL) {
        /* Never appended to; hand back an owned empty string for a uniform API. */
        char *empty = (char *)malloc(1);
        if (empty != NULL) {
            empty[0] = '\0';
        }
        return empty;
    }

    char *data = sb->data;
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return data;
}

void nixie_strbuf_free(nixie_strbuf_t *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}
