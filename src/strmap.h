#ifndef NIXIE_STRMAP_H
#define NIXIE_STRMAP_H

#include <stddef.h>

#include "arena.h"

/*
 * Small arena-backed string -> int map, used by the flowchart parser to look
 * up a node's index by id in O(1) instead of a linear scan.
 */
typedef struct nixie_strmap nixie_strmap_t;

nixie_strmap_t *nixie_strmap_create(nixie_arena_t *a, size_t bucket_count);

/* Returns -1 if the key is absent. */
int nixie_strmap_get(const nixie_strmap_t *m, const char *key, size_t len);

/* Overwrites the value if the key is already present. */
void nixie_strmap_put(nixie_strmap_t *m, const char *key, size_t len, int value);

#endif /* NIXIE_STRMAP_H */
