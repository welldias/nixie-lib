#ifndef NIXIE_FLOWCHART_MODEL_H
#define NIXIE_FLOWCHART_MODEL_H

#include <stddef.h>

#include "../strmap.h"

typedef enum nixie_direction {
    NIXIE_DIR_TD,
    NIXIE_DIR_LR,
    NIXIE_DIR_BT,
    NIXIE_DIR_RL
} nixie_direction_t;

typedef enum nixie_node_shape {
    NIXIE_SHAPE_RECTANGLE,
    NIXIE_SHAPE_ROUNDED,
    NIXIE_SHAPE_DIAMOND,
    NIXIE_SHAPE_STADIUM,
    NIXIE_SHAPE_CIRCLE,
    NIXIE_SHAPE_SUBROUTINE,
    NIXIE_SHAPE_DOUBLECIRCLE,
    NIXIE_SHAPE_HEXAGON,
    NIXIE_SHAPE_CYLINDER,
    NIXIE_SHAPE_ASYMMETRIC,
    NIXIE_SHAPE_TRAPEZOID,
    NIXIE_SHAPE_TRAPEZOID_ALT,
    /* Reserved for the future state-diagram slice; unused by the flowchart
     * parser but kept here since state diagrams reuse this exact model. */
    NIXIE_SHAPE_STATE_START,
    NIXIE_SHAPE_STATE_END
} nixie_node_shape_t;

typedef enum nixie_edge_style {
    NIXIE_EDGE_SOLID,
    NIXIE_EDGE_DOTTED,
    NIXIE_EDGE_THICK
} nixie_edge_style_t;

typedef struct nixie_mm_node {
    char *id;
    char *label;
    nixie_node_shape_t shape;
} nixie_mm_node_t;

typedef struct nixie_mm_edge {
    int source_idx;
    int target_idx;
    char *label; /* nullable */
    nixie_edge_style_t style;
    int has_arrow_start;
    int has_arrow_end;
} nixie_mm_edge_t;

typedef struct nixie_mm_graph {
    nixie_direction_t direction;

    nixie_mm_node_t *nodes;
    size_t node_count;
    size_t node_cap;

    nixie_mm_edge_t *edges;
    size_t edge_count;
    size_t edge_cap;

    nixie_strmap_t *node_index; /* id -> index in nodes[] */

    /* Subgraphs/classDef/class-assignment/style/linkStyle are out of scope
     * for this v1 slice (flowchart parser silently ignores that syntax);
     * fields are intentionally not declared here yet so adding them later
     * (when subgraph-nested layout lands) is a pure addition, not a
     * layout-breaking change for existing code. */
} nixie_mm_graph_t;

#endif /* NIXIE_FLOWCHART_MODEL_H */
