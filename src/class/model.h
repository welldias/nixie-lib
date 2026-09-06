#ifndef NIXIE_CLASS_MODEL_H
#define NIXIE_CLASS_MODEL_H

#include <stddef.h>

#include "../strmap.h"

typedef enum nixie_visibility {
    NIXIE_VIS_NONE,
    NIXIE_VIS_PUBLIC,    /* + */
    NIXIE_VIS_PRIVATE,   /* - */
    NIXIE_VIS_PROTECTED, /* # */
    NIXIE_VIS_PACKAGE    /* ~ */
} nixie_visibility_t;

typedef struct nixie_class_member {
    nixie_visibility_t visibility;
    char *name;
    char *type;   /* nullable */
    char *params; /* nullable; only set for methods */
    int is_static;
    int is_abstract;
    int is_method;
} nixie_class_member_t;

typedef struct nixie_class_node {
    char *id;
    char *label;      /* defaults to id; becomes "id<Generic>" for `class Id~Generic~` */
    char *annotation; /* nullable: <<interface>>, <<abstract>>, ... */

    nixie_class_member_t *attributes;
    size_t attribute_count;
    size_t attribute_cap;

    nixie_class_member_t *methods;
    size_t method_count;
    size_t method_cap;
} nixie_class_node_t;

typedef enum nixie_relationship_type {
    NIXIE_REL_INHERITANCE, /* <|--  hollow triangle */
    NIXIE_REL_COMPOSITION, /* *--   filled diamond */
    NIXIE_REL_AGGREGATION, /* o--   hollow diamond */
    NIXIE_REL_ASSOCIATION, /* -->   open arrow */
    NIXIE_REL_DEPENDENCY,  /* ..>   open arrow, dashed */
    NIXIE_REL_REALIZATION  /* ..|>  hollow triangle, dashed */
} nixie_relationship_type_t;

typedef enum nixie_marker_at {
    NIXIE_MARKER_FROM,
    NIXIE_MARKER_TO
} nixie_marker_at_t;

typedef struct nixie_class_relationship {
    int from_idx;
    int to_idx;
    nixie_relationship_type_t type;
    nixie_marker_at_t marker_at;
    char *label;            /* nullable */
    char *from_cardinality; /* nullable */
    char *to_cardinality;   /* nullable */
} nixie_class_relationship_t;

typedef struct nixie_class_diagram {
    nixie_class_node_t *classes;
    size_t class_count;
    size_t class_cap;

    nixie_class_relationship_t *relationships;
    size_t relationship_count;
    size_t relationship_cap;

    nixie_strmap_t *class_index; /* id -> index in classes[] */

    /* Namespaces are recognized by the parser just enough to not break
     * parsing (their classes are flattened into the top-level list, no
     * visual grouping box) -- same scoping decision already made for
     * flowchart subgraphs and state-diagram composite states. */
} nixie_class_diagram_t;

#endif /* NIXIE_CLASS_MODEL_H */
