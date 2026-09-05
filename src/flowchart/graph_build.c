#include "graph_build.h"

#include <string.h>

#include "../strmap.h"

nixie_sig_lines_t nixie_split_significant_lines(nixie_arena_t *arena, const char *text) {
    nixie_sig_lines_t result;
    result.lines = NULL;
    result.count = 0;

    if (text == NULL) {
        text = "";
    }

    size_t cap = 16, count = 0;
    nixie_sig_line_t *lines = (nixie_sig_line_t *)nixie_arena_alloc(arena, cap * sizeof(nixie_sig_line_t));

    const char *p = text;
    int line_no = 0;
    for (;;) {
        line_no++;
        const char *line_start = p;
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        size_t raw_len = (size_t)(p - line_start);

        size_t start = 0, end = raw_len;
        while (start < end && (line_start[start] == ' ' || line_start[start] == '\t' || line_start[start] == '\r')) {
            start++;
        }
        while (end > start && (line_start[end - 1] == ' ' || line_start[end - 1] == '\t' || line_start[end - 1] == '\r')) {
            end--;
        }

        int is_comment = (end - start >= 2) && line_start[start] == '%' && line_start[start + 1] == '%';
        if (end > start && !is_comment) {
            if (count == cap) {
                size_t new_cap = cap * 2;
                nixie_sig_line_t *new_lines =
                    (nixie_sig_line_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_sig_line_t));
                memcpy(new_lines, lines, count * sizeof(nixie_sig_line_t));
                lines = new_lines;
                cap = new_cap;
            }
            lines[count].content = nixie_arena_strndup(arena, line_start + start, end - start);
            lines[count].line_no = line_no;
            count++;
        }

        if (*p == '\0') {
            break;
        }
        p++;
    }

    result.lines = lines;
    result.count = count;
    return result;
}

void nixie_mm_ensure_node_capacity(nixie_arena_t *arena, nixie_mm_graph_t *g) {
    if (g->node_count < g->node_cap) {
        return;
    }
    size_t new_cap = g->node_cap == 0 ? 8 : g->node_cap * 2;
    nixie_mm_node_t *new_nodes = (nixie_mm_node_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_mm_node_t));
    if (g->node_count > 0) {
        memcpy(new_nodes, g->nodes, g->node_count * sizeof(nixie_mm_node_t));
    }
    g->nodes = new_nodes;
    g->node_cap = new_cap;
}

void nixie_mm_ensure_edge_capacity(nixie_arena_t *arena, nixie_mm_graph_t *g) {
    if (g->edge_count < g->edge_cap) {
        return;
    }
    size_t new_cap = g->edge_cap == 0 ? 8 : g->edge_cap * 2;
    nixie_mm_edge_t *new_edges = (nixie_mm_edge_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_mm_edge_t));
    if (g->edge_count > 0) {
        memcpy(new_edges, g->edges, g->edge_count * sizeof(nixie_mm_edge_t));
    }
    g->edges = new_edges;
    g->edge_cap = new_cap;
}

int nixie_mm_find_or_add_node(
    nixie_arena_t *arena, nixie_mm_graph_t *g, const char *id, size_t id_len, char *label,
    nixie_node_shape_t shape) {
    int existing = nixie_strmap_get(g->node_index, id, id_len);
    if (existing >= 0) {
        return existing;
    }

    nixie_mm_ensure_node_capacity(arena, g);
    int idx = (int)g->node_count;

    char *id_copy = nixie_arena_strndup(arena, id, id_len);
    g->nodes[idx].id = id_copy;
    g->nodes[idx].label = label != NULL ? label : nixie_arena_strdup(arena, id_copy);
    g->nodes[idx].shape = shape;
    g->node_count++;

    nixie_strmap_put(g->node_index, id, id_len, idx);
    return idx;
}
