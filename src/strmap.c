#include "strmap.h"

#include <string.h>

typedef struct nixie_strmap_entry {
    const char *key;
    size_t key_len;
    int value;
    struct nixie_strmap_entry *next;
} nixie_strmap_entry_t;

struct nixie_strmap {
    nixie_arena_t *arena;
    nixie_strmap_entry_t **buckets;
    size_t bucket_count;
};

static size_t hash_key(const char *key, size_t len) {
    /* FNV-1a */
    size_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)key[i];
        h *= 16777619u;
    }
    return h;
}

nixie_strmap_t *nixie_strmap_create(nixie_arena_t *a, size_t bucket_count) {
    if (bucket_count == 0) {
        bucket_count = 64;
    }

    nixie_strmap_t *m = (nixie_strmap_t *)nixie_arena_alloc(a, sizeof(nixie_strmap_t));
    if (m == NULL) {
        return NULL;
    }

    m->arena = a;
    m->bucket_count = bucket_count;
    m->buckets = (nixie_strmap_entry_t **)nixie_arena_alloc_zeroed(
        a, bucket_count * sizeof(nixie_strmap_entry_t *));
    if (m->buckets == NULL) {
        return NULL;
    }

    return m;
}

int nixie_strmap_get(const nixie_strmap_t *m, const char *key, size_t len) {
    if (m == NULL) {
        return -1;
    }

    size_t idx = hash_key(key, len) % m->bucket_count;
    for (nixie_strmap_entry_t *e = m->buckets[idx]; e != NULL; e = e->next) {
        if (e->key_len == len && memcmp(e->key, key, len) == 0) {
            return e->value;
        }
    }
    return -1;
}

void nixie_strmap_put(nixie_strmap_t *m, const char *key, size_t len, int value) {
    if (m == NULL) {
        return;
    }

    size_t idx = hash_key(key, len) % m->bucket_count;
    for (nixie_strmap_entry_t *e = m->buckets[idx]; e != NULL; e = e->next) {
        if (e->key_len == len && memcmp(e->key, key, len) == 0) {
            e->value = value;
            return;
        }
    }

    nixie_strmap_entry_t *entry = (nixie_strmap_entry_t *)nixie_arena_alloc(
        m->arena, sizeof(nixie_strmap_entry_t));
    entry->key = nixie_arena_strndup(m->arena, key, len);
    entry->key_len = len;
    entry->value = value;
    entry->next = m->buckets[idx];
    m->buckets[idx] = entry;
}
