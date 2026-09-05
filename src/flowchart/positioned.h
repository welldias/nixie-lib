#ifndef NIXIE_FLOWCHART_POSITIONED_H
#define NIXIE_FLOWCHART_POSITIONED_H

#include <stddef.h>

#include "../geometry.h"
#include "model.h"

typedef struct nixie_pf_node {
    int index;
    const char *id;
    const char *label;
    nixie_node_shape_t shape;
    double x, y, w, h;
    /* Layer index from the layered layout engine. The SVG renderer ignores
     * this; the ASCII renderer uses it to regroup nodes into character-grid
     * rows/columns without having to re-derive layering from pixel
     * positions (which the coordinate-assignment phase's per-node centering
     * makes an unreliable clustering problem). */
    int layer;
} nixie_pf_node_t;

typedef struct nixie_pf_edge {
    int source_idx;
    int target_idx;
    const char *label; /* nullable */
    nixie_edge_style_t style;
    int has_arrow_start;
    int has_arrow_end;
    nixie_point_t *points;
    size_t point_count;
    nixie_point_t label_pos;
    int has_label_pos; /* if 0, renderer falls back to the polyline's geometric midpoint */
} nixie_pf_edge_t;

typedef struct nixie_positioned_flowchart {
    double width;
    double height;
    nixie_direction_t direction;
    nixie_pf_node_t *nodes;
    size_t node_count;
    nixie_pf_edge_t *edges;
    size_t edge_count;
    /* groups (subgraph boxes): omitted in v1, added when subgraph layout lands. */
} nixie_positioned_flowchart_t;

#endif /* NIXIE_FLOWCHART_POSITIONED_H */
