#ifndef NIXIE_XYCHART_PARSER_H
#define NIXIE_XYCHART_PARSER_H

#include <nixie/nixie.h>

#include "../arena.h"
#include "model.h"

typedef struct nixie_xy_parse_result {
    nixie_xy_chart_t *chart; /* NULL on error */
    nixie_error_t error;
    char error_message[256];
    int error_line; /* 1-based, -1 if not applicable */
} nixie_xy_parse_result_t;

/* Parses xychart-beta mermaid text into a nixie_xy_chart_t. All allocations
 * come from `arena`. */
nixie_xy_parse_result_t nixie_xy_parse(nixie_arena_t *arena, const char *text);

#endif /* NIXIE_XYCHART_PARSER_H */
