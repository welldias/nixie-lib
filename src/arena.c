#include "arena.h"

#include <stdlib.h>
#include <string.h>

/* Alignment large enough for any standard type (double, pointers, etc.). */
union nixie_arena_align_helper {
    double d;
    void *p;
    long l;
};
#define NIXIE_ARENA_ALIGN sizeof(union nixie_arena_align_helper)

typedef struct nixie_arena_block {
    struct nixie_arena_block *next;
    size_t size; /* usable byte capacity of data[] */
    size_t used;
    unsigned char data[1];
} nixie_arena_block_t;

struct nixie_arena {
    nixie_arena_block_t *head; /* most recently allocated block */
    size_t default_block_size;
};

static size_t align_up(size_t n) {
    size_t rem = n % NIXIE_ARENA_ALIGN;
    return rem == 0 ? n : n + (NIXIE_ARENA_ALIGN - rem);
}

static nixie_arena_block_t *arena_block_create(size_t size) {
    nixie_arena_block_t *block = (nixie_arena_block_t *)malloc(sizeof(nixie_arena_block_t) - 1 + size);
    if (block == NULL) {
        return NULL;
    }
    block->next = NULL;
    block->size = size;
    block->used = 0;
    return block;
}

nixie_arena_t *nixie_arena_create(size_t initial_block_size) {
    if (initial_block_size == 0) {
        initial_block_size = 4096;
    }

    nixie_arena_t *a = (nixie_arena_t *)malloc(sizeof(nixie_arena_t));
    if (a == NULL) {
        return NULL;
    }

    a->default_block_size = initial_block_size;
    a->head               = arena_block_create(initial_block_size);
    if (a->head == NULL) {
        free(a);
        return NULL;
    }

    return a;
}

void *nixie_arena_alloc(nixie_arena_t *a, size_t size) {
    if (a == NULL || size == 0) {
        return NULL;
    }

    size_t aligned = align_up(size);

    if (a->head == NULL || a->head->used + aligned > a->head->size) {
        size_t block_size          = aligned > a->default_block_size ? aligned : a->default_block_size;
        nixie_arena_block_t *block = arena_block_create(block_size);
        if (block == NULL) {
            return NULL;
        }
        block->next = a->head;
        a->head     = block;
    }

    void *ptr = a->head->data + a->head->used;
    a->head->used += aligned;
    return ptr;
}

void *nixie_arena_alloc_zeroed(nixie_arena_t *a, size_t size) {
    void *ptr = nixie_arena_alloc(a, size);
    if (ptr != NULL) {
        memset(ptr, 0, size);
    }
    return ptr;
}

char *nixie_arena_strdup(nixie_arena_t *a, const char *s) {
    if (s == NULL) {
        return NULL;
    }
    return nixie_arena_strndup(a, s, strlen(s));
}

char *nixie_arena_strndup(nixie_arena_t *a, const char *s, size_t n) {
    if (s == NULL) {
        return NULL;
    }
    char *copy = (char *)nixie_arena_alloc(a, n + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

void nixie_arena_destroy(nixie_arena_t *a) {
    if (a == NULL) {
        return;
    }
    nixie_arena_block_t *block = a->head;
    while (block != NULL) {
        nixie_arena_block_t *next = block->next;
        free(block);
        block = next;
    }
    free(a);
}
