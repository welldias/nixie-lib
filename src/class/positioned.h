#ifndef NIXIE_CLASS_POSITIONED_H
#define NIXIE_CLASS_POSITIONED_H

#include <stddef.h>

#include "../geometry.h"
#include "model.h"

typedef struct nixie_pc_node {
    int index;
    const char *id;
    const char *label;
    const char *annotation; /* nullable */
    const nixie_class_member_t *attributes;
    size_t attribute_count;
    const nixie_class_member_t *methods;
    size_t method_count;

    double x, y, w, h;
    double header_h, attr_h, method_h;

    /* Layer index from the layered layout engine; unused by the SVG
     * renderer, kept for a future ASCII backend the way flowchart's
     * positioned model does. */
    int layer;
} nixie_pc_node_t;

typedef struct nixie_pc_relationship {
    int from_idx;
    int to_idx;
    nixie_relationship_type_t type;
    nixie_marker_at_t marker_at;
    const char *label;            /* nullable */
    const char *from_cardinality; /* nullable */
    const char *to_cardinality;   /* nullable */
    nixie_point_t *points;
    size_t point_count;
} nixie_pc_relationship_t;

typedef struct nixie_positioned_class_diagram {
    double width;
    double height;
    nixie_pc_node_t *nodes;
    size_t node_count;
    nixie_pc_relationship_t *relationships;
    size_t relationship_count;
} nixie_positioned_class_diagram_t;

#endif /* NIXIE_CLASS_POSITIONED_H */
