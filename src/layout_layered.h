#ifndef NIXIE_LAYOUT_LAYERED_H
#define NIXIE_LAYOUT_LAYERED_H

#include <stddef.h>

#include "arena.h"
#include "geometry.h"

/*
 * Simplified layered ("Sugiyama-lite") graph layout engine, shared by every
 * graph-shaped diagram type (flowchart now; state/class/ER later). This is
 * intentionally NOT a port of elkjs or a full Brandes-Koepf implementation --
 * see the project plan for the scoped-down phases this performs:
 * cycle-breaking, longest-path layer assignment, a few barycenter crossing-
 * reduction sweeps, simple centered coordinate assignment, and basic
 * orthogonal edge routing.
 */

typedef enum nixie_lg_direction {
    NIXIE_LG_DOWN,
    NIXIE_LG_UP,
    NIXIE_LG_LEFT,
    NIXIE_LG_RIGHT
} nixie_lg_direction_t;

typedef struct nixie_lg_node {
    /* input */
    double width;
    double height;
    /* computed */
    int layer;
    int order; /* position within its layer, 0-indexed */
    double x;  /* computed, top-left */
    double y;  /* computed, top-left */
} nixie_lg_node_t;

typedef struct nixie_lg_edge {
    int from; /* node index */
    int to;   /* node index */
    int reversed; /* set by nixie_lg_break_cycles(); used only during layering */
} nixie_lg_edge_t;

typedef struct nixie_lg_graph {
    nixie_lg_node_t *nodes;
    size_t node_count;
    nixie_lg_edge_t *edges;
    size_t edge_count;
    nixie_lg_direction_t direction;
    double node_spacing;  /* gap between nodes within the same layer */
    double layer_spacing; /* gap between layers */
} nixie_lg_graph_t;

/* Each phase can also be run standalone (e.g. for unit tests); they must be
 * run in this order since each depends on the previous phase's output. */
void nixie_lg_break_cycles(nixie_lg_graph_t *g);
void nixie_lg_assign_layers(nixie_lg_graph_t *g);
void nixie_lg_reduce_crossings(nixie_lg_graph_t *g, int iterations);
void nixie_lg_assign_coordinates(nixie_lg_graph_t *g);

/* Runs all four phases in order. */
void nixie_lg_layout(nixie_lg_graph_t *g);

typedef struct nixie_lg_points {
    nixie_point_t *points;
    size_t count;
} nixie_lg_points_t;

/* Produces an orthogonal polyline connecting the boundary of edge->from to
 * the boundary of edge->to, based on their already-computed positions. Must
 * be called after nixie_lg_assign_coordinates(). Points are allocated from
 * `arena`. */
nixie_lg_points_t nixie_lg_route_edge(
    nixie_arena_t *arena, const nixie_lg_graph_t *g, const nixie_lg_edge_t *e);

#endif /* NIXIE_LAYOUT_LAYERED_H */
