#ifndef NIXIE_FLOWCHART_LAYOUT_H
#define NIXIE_FLOWCHART_LAYOUT_H

#include "../arena.h"
#include "model.h"
#include "positioned.h"

typedef struct nixie_layout_options {
    double padding;
    double node_spacing;
    double layer_spacing;
} nixie_layout_options_t;

/* opts may be NULL to use sensible defaults. */
nixie_positioned_flowchart_t *nixie_flowchart_layout(
    nixie_arena_t *arena, const nixie_mm_graph_t *graph, const nixie_layout_options_t *opts);

#endif /* NIXIE_FLOWCHART_LAYOUT_H */
