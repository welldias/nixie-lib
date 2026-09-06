#ifndef NIXIE_FLOWCHART_GRAPH_BUILD_H
#define NIXIE_FLOWCHART_GRAPH_BUILD_H

#include <stddef.h>

#include "../arena.h"
#include "model.h"

typedef struct nixie_sig_line {
    char *content;
    int line_no;
} nixie_sig_line_t;

typedef struct nixie_sig_lines {
    nixie_sig_line_t *lines;
    size_t count;
} nixie_sig_lines_t;

/*
 * Splits text into trimmed, non-blank, non-"%%"-comment lines, each copied
 * into an arena-owned NUL-terminated buffer, tracking original 1-based line
 * numbers for error reporting. Shared by every nixie_mm_graph_t-producing
 * parser (flowchart, state, ...).
 */
nixie_sig_lines_t nixie_split_significant_lines(nixie_arena_t *arena, const char *text);

void nixie_mm_ensure_node_capacity(nixie_arena_t *arena, nixie_mm_graph_t *g);
void nixie_mm_ensure_edge_capacity(nixie_arena_t *arena, nixie_mm_graph_t *g);

/*
 * Registers a node the first time its id is seen; subsequent calls with the
 * same id return the existing index unchanged ("first definition wins",
 * mirroring beautiful-mermaid/src/parser.ts's registerNode()). `label` may
 * be NULL, meaning "use the id itself as the label".
 */
int nixie_mm_find_or_add_node(nixie_arena_t *arena, nixie_mm_graph_t *g, const char *id, size_t id_len, char *label, nixie_node_shape_t shape);

#endif /* NIXIE_FLOWCHART_GRAPH_BUILD_H */
