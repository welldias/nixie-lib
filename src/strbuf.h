#ifndef NIXIE_STRBUF_H
#define NIXIE_STRBUF_H

#include <stddef.h>

/*
 * Dynamic, malloc/realloc-backed string builder. Used to build the final
 * output string returned to the caller (SVG or ASCII text) -- deliberately
 * independent from the pipeline arena, so ownership of the returned string
 * is a single plain pointer the caller frees with nixie_free().
 */
typedef struct nixie_strbuf {
    char *data;
    size_t len;
    size_t cap;
} nixie_strbuf_t;

void nixie_strbuf_init(nixie_strbuf_t *sb);
int nixie_strbuf_append(nixie_strbuf_t *sb, const char *text);
int nixie_strbuf_append_n(nixie_strbuf_t *sb, const char *text, size_t n);
int nixie_strbuf_append_char(nixie_strbuf_t *sb, char c);
int nixie_strbuf_appendf(nixie_strbuf_t *sb, const char *fmt, ...);
char *nixie_strbuf_release(nixie_strbuf_t *sb);
void nixie_strbuf_free(nixie_strbuf_t *sb);

#endif /* NIXIE_STRBUF_H */
