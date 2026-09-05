#ifndef NIXIE_ARENA_H
#define NIXIE_ARENA_H

#include <stddef.h>

/*
 * Bump allocator used for the lifetime of a single parse->layout->render
 * pipeline. Every allocation the pipeline makes (parse model, positioned
 * model, layout scratch space) comes from one arena that is destroyed in a
 * single call once the pipeline is done, regardless of success or failure.
 */
typedef struct nixie_arena nixie_arena_t;

nixie_arena_t *nixie_arena_create(size_t initial_block_size);
void *nixie_arena_alloc(nixie_arena_t *a, size_t size);
void *nixie_arena_alloc_zeroed(nixie_arena_t *a, size_t size);
char *nixie_arena_strdup(nixie_arena_t *a, const char *s);
char *nixie_arena_strndup(nixie_arena_t *a, const char *s, size_t n);
void nixie_arena_destroy(nixie_arena_t *a);

#endif /* NIXIE_ARENA_H */
