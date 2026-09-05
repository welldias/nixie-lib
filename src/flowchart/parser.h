#ifndef NIXIE_FLOWCHART_PARSER_H
#define NIXIE_FLOWCHART_PARSER_H

#include <nixie/nixie.h>

#include "../arena.h"
#include "model.h"

typedef struct nixie_parse_result {
    nixie_mm_graph_t *graph; /* NULL on error */
    nixie_error_t error;
    char error_message[256];
    int error_line; /* 1-based, -1 if not applicable */
} nixie_parse_result_t;

/* Parses flowchart/graph mermaid text ("graph TD" / "flowchart LR" header)
 * into a nixie_mm_graph_t. All allocations (graph, nodes, edges, labels)
 * come from `arena`. */
nixie_parse_result_t nixie_flowchart_parse(nixie_arena_t *arena, const char *text);

#endif /* NIXIE_FLOWCHART_PARSER_H */
