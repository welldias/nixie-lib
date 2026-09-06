#ifndef NIXIE_ER_MODEL_H
#define NIXIE_ER_MODEL_H

#include <stddef.h>

#include "../strmap.h"

#define NIXIE_ER_KEY_PK 0x1
#define NIXIE_ER_KEY_FK 0x2
#define NIXIE_ER_KEY_UK 0x4

typedef struct nixie_er_attribute {
    char *type;
    char *name;
    int keys;      /* bitmask of NIXIE_ER_KEY_* */
    char *comment; /* nullable */
} nixie_er_attribute_t;

typedef struct nixie_er_entity {
    char *id;
    char *label; /* defaults to id */

    nixie_er_attribute_t *attributes;
    size_t attribute_count;
    size_t attribute_cap;
} nixie_er_entity_t;

/* Crow's-foot cardinality, per beautiful-mermaid/src/er/types.ts's
 * Cardinality: 'one' (||), 'zero-one' (o|/|o), 'many' (}|//|{), 'zero-many'
 * (o{/{o). */
typedef enum nixie_er_cardinality {
    NIXIE_ER_ONE,
    NIXIE_ER_ZERO_ONE,
    NIXIE_ER_MANY,
    NIXIE_ER_ZERO_MANY
} nixie_er_cardinality_t;

typedef struct nixie_er_relationship {
    int entity1_idx;
    int entity2_idx;
    nixie_er_cardinality_t cardinality1; /* at entity1's end */
    nixie_er_cardinality_t cardinality2; /* at entity2's end */
    char *label;
    int identifying; /* 1 = solid line (--), 0 = dashed (..) */
} nixie_er_relationship_t;

typedef struct nixie_er_diagram {
    nixie_er_entity_t *entities;
    size_t entity_count;
    size_t entity_cap;

    nixie_er_relationship_t *relationships;
    size_t relationship_count;
    size_t relationship_cap;

    nixie_strmap_t *entity_index; /* id -> index in entities[] */
} nixie_er_diagram_t;

#endif /* NIXIE_ER_MODEL_H */
