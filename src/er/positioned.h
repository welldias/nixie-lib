#ifndef NIXIE_ER_POSITIONED_H
#define NIXIE_ER_POSITIONED_H

#include <stddef.h>

#include "../geometry.h"
#include "model.h"

typedef struct nixie_pe_node {
    int index;
    const char *id;
    const char *label;
    const nixie_er_attribute_t *attributes;
    size_t attribute_count;

    double x, y, w, h;
    double header_h, row_h;

    /* Layer index from the layered layout engine; the SVG renderer ignores
     * this, the ASCII renderer uses it (same pattern as flowchart/class). */
    int layer;
} nixie_pe_node_t;

typedef struct nixie_pe_relationship {
    int entity1_idx;
    int entity2_idx;
    nixie_er_cardinality_t cardinality1;
    nixie_er_cardinality_t cardinality2;
    const char *label; /* nullable */
    int identifying;
    nixie_point_t *points;
    size_t point_count;
} nixie_pe_relationship_t;

typedef struct nixie_positioned_er_diagram {
    double width, height;
    nixie_pe_node_t *nodes;
    size_t node_count;
    nixie_pe_relationship_t *relationships;
    size_t relationship_count;
} nixie_positioned_er_diagram_t;

#endif /* NIXIE_ER_POSITIONED_H */
