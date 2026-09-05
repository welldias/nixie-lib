#ifndef NIXIE_ER_PARSER_H
#define NIXIE_ER_PARSER_H

#include <nixie/nixie.h>

#include "../arena.h"
#include "model.h"

typedef struct nixie_er_parse_result {
    nixie_er_diagram_t *diagram; /* NULL on error */
    nixie_error_t error;
    char error_message[256];
    int error_line; /* 1-based, -1 if not applicable */
} nixie_er_parse_result_t;

/* Parses erDiagram mermaid text into a nixie_er_diagram_t. All allocations
 * come from `arena`. */
nixie_er_parse_result_t nixie_er_parse(nixie_arena_t *arena, const char *text);

#endif /* NIXIE_ER_PARSER_H */
