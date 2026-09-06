#ifndef NIXIE_TEXT_METRICS_H
#define NIXIE_TEXT_METRICS_H

#include <stddef.h>
#include <stdint.h>

#include "arena.h"

/* Standard line height ratio for multi-line text (1.3 = 130% of font size),
 * ported from beautiful-mermaid/src/text-metrics.ts's LINE_HEIGHT_RATIO. */
#define NIXIE_LINE_HEIGHT_RATIO 1.3

/* Decodes one UTF-8 codepoint starting at s. Returns the number of bytes
 * consumed (1-4); on malformed input, consumes 1 byte and yields that byte's
 * value as the codepoint, so callers always make forward progress. */
size_t nixie_utf8_decode(const char *s, uint32_t *cp_out);

/* Font-agnostic relative width of a single codepoint, using the same
 * character-class buckets as beautiful-mermaid/src/text-metrics.ts's
 * getCharWidth() (calibrated for the Inter font family). */
double nixie_char_width(uint32_t codepoint);

/* Estimated pixel width of a single-line, plain UTF-8 string (no embedded
 * newlines). fontWeight follows CSS numeric weights (400 regular, 500
 * medium, 600 semibold, ...). */
double nixie_measure_text_width(const char *text, double font_size, int font_weight);

typedef struct nixie_multiline_metrics {
    double width;
    double height;
    double line_height;
    const char **lines; /* arena-owned array of arena-owned, NUL-terminated lines */
    size_t line_count;
} nixie_multiline_metrics_t;

/* Splits text on '\n' and measures it, mirroring
 * beautiful-mermaid/src/text-metrics.ts's measureMultilineText(). */
nixie_multiline_metrics_t nixie_measure_multiline(nixie_arena_t *arena, const char *text, double font_size, int font_weight);

#endif /* NIXIE_TEXT_METRICS_H */
