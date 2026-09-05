#ifndef NIXIE_CLASS_PARSER_H
#define NIXIE_CLASS_PARSER_H

#include <nixie/nixie.h>

#include "../arena.h"
#include "model.h"

typedef struct nixie_class_parse_result {
    nixie_class_diagram_t *diagram; /* NULL on error */
    nixie_error_t error;
    char error_message[256];
    int error_line; /* 1-based, -1 if not applicable */
} nixie_class_parse_result_t;

/* Parses classDiagram mermaid text into a nixie_class_diagram_t. All
 * allocations come from `arena`. */
nixie_class_parse_result_t nixie_class_parse(nixie_arena_t *arena, const char *text);

#endif /* NIXIE_CLASS_PARSER_H */
