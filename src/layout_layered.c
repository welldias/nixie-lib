#include "layout_layered.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * Phase 1: cycle breaking (DFS back-edge detection)
 * ========================================================================== */

typedef enum { DFS_WHITE, DFS_GRAY, DFS_BLACK } dfs_color_t;

typedef struct {
    nixie_lg_graph_t *g;
    dfs_color_t *color;
} cycle_ctx_t;

static void dfs_break_cycles(cycle_ctx_t *ctx, int u) {
    ctx->color[u] = DFS_GRAY;
    for (size_t i = 0; i < ctx->g->edge_count; i++) {
        nixie_lg_edge_t *e = &ctx->g->edges[i];
        if (e->from != u || e->from == e->to) {
            continue;
        }
        int v = e->to;
        if (ctx->color[v] == DFS_GRAY) {
            e->reversed = 1;
        } else if (ctx->color[v] == DFS_WHITE) {
            dfs_break_cycles(ctx, v);
        }
    }
    ctx->color[u] = DFS_BLACK;
}

void nixie_lg_break_cycles(nixie_lg_graph_t *g) {
    if (g->node_count == 0) {
        return;
    }

    for (size_t i = 0; i < g->edge_count; i++) {
        g->edges[i].reversed = 0;
    }

    dfs_color_t *color = (dfs_color_t *)calloc(g->node_count, sizeof(dfs_color_t));
    cycle_ctx_t ctx;
    ctx.g = g;
    ctx.color = color;

    for (size_t u = 0; u < g->node_count; u++) {
        if (color[u] == DFS_WHITE) {
            dfs_break_cycles(&ctx, (int)u);
        }
    }

    free(color);
}

/* ==========================================================================
 * Phase 2: layer assignment (longest path, via Kahn's algorithm on the
 * effective DAG produced by phase 1)
 * ========================================================================== */

static int lg_max_layer(const nixie_lg_graph_t *g) {
    int max_l = 0;
    for (size_t i = 0; i < g->node_count; i++) {
        if (g->nodes[i].layer > max_l) {
            max_l = g->nodes[i].layer;
        }
    }
    return max_l;
}

void nixie_lg_assign_layers(nixie_lg_graph_t *g) {
    size_t n = g->node_count;
    if (n == 0) {
        return;
    }

    int *indeg = (int *)calloc(n, sizeof(int));
    for (size_t i = 0; i < g->edge_count; i++) {
        const nixie_lg_edge_t *e = &g->edges[i];
        if (e->from == e->to) {
            continue;
        }
        int eff_to = e->reversed ? e->from : e->to;
        indeg[eff_to]++;
    }

    int *queue = (int *)malloc(n * sizeof(int));
    size_t qh = 0, qt = 0;
    for (size_t i = 0; i < n; i++) {
        g->nodes[i].layer = 0;
        if (indeg[i] == 0) {
            queue[qt++] = (int)i;
        }
    }

    while (qh < qt) {
        int u = queue[qh++];
        for (size_t i = 0; i < g->edge_count; i++) {
            const nixie_lg_edge_t *e = &g->edges[i];
            if (e->from == e->to) {
                continue;
            }
            int eff_from = e->reversed ? e->to : e->from;
            int eff_to = e->reversed ? e->from : e->to;
            if (eff_from != u) {
                continue;
            }
            if (g->nodes[eff_to].layer < g->nodes[u].layer + 1) {
                g->nodes[eff_to].layer = g->nodes[u].layer + 1;
            }
            if (--indeg[eff_to] == 0) {
                queue[qt++] = eff_to;
            }
        }
    }

    free(indeg);
    free(queue);
}

/* ==========================================================================
 * Phase 3: crossing reduction (barycenter sweeps)
 * ========================================================================== */

typedef struct {
    int node;
    double bary;
    int orig_order;
} bary_entry_t;

static int bary_cmp(const void *a, const void *b) {
    const bary_entry_t *ea = (const bary_entry_t *)a;
    const bary_entry_t *eb = (const bary_entry_t *)b;
    if (ea->bary < eb->bary) return -1;
    if (ea->bary > eb->bary) return 1;
    if (ea->orig_order < eb->orig_order) return -1;
    if (ea->orig_order > eb->orig_order) return 1;
    return 0;
}

static void reorder_layer(nixie_lg_graph_t *g, int **layer_nodes, const int *layer_sizes, int layer, int ref_layer) {
    int size = layer_sizes[layer];
    if (size == 0) {
        return;
    }

    bary_entry_t *entries = (bary_entry_t *)malloc((size_t)size * sizeof(bary_entry_t));

    for (int i = 0; i < size; i++) {
        int node = layer_nodes[layer][i];
        double sum = 0.0;
        int count = 0;

        for (size_t ei = 0; ei < g->edge_count; ei++) {
            const nixie_lg_edge_t *e = &g->edges[ei];
            int other = -1;
            if (e->from == node && g->nodes[e->to].layer == ref_layer) {
                other = e->to;
            } else if (e->to == node && g->nodes[e->from].layer == ref_layer) {
                other = e->from;
            }
            if (other >= 0) {
                sum += g->nodes[other].order;
                count++;
            }
        }

        entries[i].node = node;
        entries[i].orig_order = i;
        entries[i].bary = count > 0 ? sum / count : (double)i;
    }

    qsort(entries, (size_t)size, sizeof(bary_entry_t), bary_cmp);

    for (int i = 0; i < size; i++) {
        layer_nodes[layer][i] = entries[i].node;
        g->nodes[entries[i].node].order = i;
    }

    free(entries);
}

void nixie_lg_reduce_crossings(nixie_lg_graph_t *g, int iterations) {
    size_t n = g->node_count;
    if (n == 0) {
        return;
    }

    int layer_count = lg_max_layer(g) + 1;

    int *layer_sizes = (int *)calloc((size_t)layer_count, sizeof(int));
    for (size_t i = 0; i < n; i++) {
        layer_sizes[g->nodes[i].layer]++;
    }

    /* Initial order: stable by node index within each layer. */
    int *fill_cursor = (int *)calloc((size_t)layer_count, sizeof(int));
    for (size_t i = 0; i < n; i++) {
        int l = g->nodes[i].layer;
        g->nodes[i].order = fill_cursor[l]++;
    }
    free(fill_cursor);

    int **layer_nodes = (int **)malloc((size_t)layer_count * sizeof(int *));
    for (int l = 0; l < layer_count; l++) {
        layer_nodes[l] = layer_sizes[l] > 0 ? (int *)malloc((size_t)layer_sizes[l] * sizeof(int)) : NULL;
    }
    for (size_t i = 0; i < n; i++) {
        int l = g->nodes[i].layer;
        layer_nodes[l][g->nodes[i].order] = (int)i;
    }

    for (int iter = 0; iter < iterations; iter++) {
        if (iter % 2 == 0) {
            for (int l = 1; l < layer_count; l++) {
                reorder_layer(g, layer_nodes, layer_sizes, l, l - 1);
            }
        } else {
            for (int l = layer_count - 2; l >= 0; l--) {
                reorder_layer(g, layer_nodes, layer_sizes, l, l + 1);
            }
        }
    }

    for (int l = 0; l < layer_count; l++) {
        free(layer_nodes[l]);
    }
    free(layer_nodes);
    free(layer_sizes);
}

/* ==========================================================================
 * Phase 4: coordinate assignment
 * ========================================================================== */

void nixie_lg_assign_coordinates(nixie_lg_graph_t *g) {
    size_t n = g->node_count;
    if (n == 0) {
        return;
    }

    int layer_count = lg_max_layer(g) + 1;
    int horizontal_primary = (g->direction == NIXIE_LG_LEFT || g->direction == NIXIE_LG_RIGHT);

    double *primary_extent = (double *)calloc((size_t)layer_count, sizeof(double));
    int *layer_sizes = (int *)calloc((size_t)layer_count, sizeof(int));

    for (size_t i = 0; i < n; i++) {
        int l = g->nodes[i].layer;
        double psize = horizontal_primary ? g->nodes[i].width : g->nodes[i].height;
        if (psize > primary_extent[l]) {
            primary_extent[l] = psize;
        }
        layer_sizes[l]++;
    }

    double *layer_offset = (double *)calloc((size_t)layer_count, sizeof(double));
    for (int l = 1; l < layer_count; l++) {
        layer_offset[l] = layer_offset[l - 1] + primary_extent[l - 1] + g->layer_spacing;
    }
    double total_primary = layer_offset[layer_count - 1] + primary_extent[layer_count - 1];

    /* Cross axis: within each layer, lay nodes out in `order`, storing the
     * running position temporarily in the field that will end up unused
     * until the final direction-dependent assignment below. */
    double *layer_cross_extent = (double *)calloc((size_t)layer_count, sizeof(double));

    for (int l = 0; l < layer_count; l++) {
        int count = layer_sizes[l];
        if (count == 0) {
            continue;
        }

        int *idxs = (int *)malloc((size_t)count * sizeof(int));
        int c = 0;
        for (size_t i = 0; i < n; i++) {
            if (g->nodes[i].layer == l) {
                idxs[c++] = (int)i;
            }
        }

        /* Insertion sort by `order` -- layers are small, and this keeps the
         * result deterministic without pulling in qsort's non-stability. */
        for (int a = 1; a < count; a++) {
            int key = idxs[a];
            int b = a - 1;
            while (b >= 0 && g->nodes[idxs[b]].order > g->nodes[key].order) {
                idxs[b + 1] = idxs[b];
                b--;
            }
            idxs[b + 1] = key;
        }

        double cross_pos = 0.0;
        for (int a = 0; a < count; a++) {
            int node = idxs[a];
            double csize = horizontal_primary ? g->nodes[node].height : g->nodes[node].width;
            if (horizontal_primary) {
                g->nodes[node].y = cross_pos; /* temp */
            } else {
                g->nodes[node].x = cross_pos; /* temp */
            }
            cross_pos += csize + g->node_spacing;
        }

        layer_cross_extent[l] = count > 0 ? cross_pos - g->node_spacing : 0.0;
        free(idxs);
    }

    double max_cross_extent = 0.0;
    for (int l = 0; l < layer_count; l++) {
        if (layer_cross_extent[l] > max_cross_extent) {
            max_cross_extent = layer_cross_extent[l];
        }
    }

    for (size_t i = 0; i < n; i++) {
        int l = g->nodes[i].layer;
        double center_shift = (max_cross_extent - layer_cross_extent[l]) / 2.0;
        double psize = horizontal_primary ? g->nodes[i].width : g->nodes[i].height;
        double primary_pos = layer_offset[l] + (primary_extent[l] - psize) / 2.0;
        double cross_pos = (horizontal_primary ? g->nodes[i].y : g->nodes[i].x) + center_shift;

        switch (g->direction) {
            case NIXIE_LG_DOWN:
                g->nodes[i].x = cross_pos;
                g->nodes[i].y = primary_pos;
                break;
            case NIXIE_LG_UP:
                g->nodes[i].x = cross_pos;
                g->nodes[i].y = total_primary - primary_pos - psize;
                break;
            case NIXIE_LG_RIGHT:
                g->nodes[i].x = primary_pos;
                g->nodes[i].y = cross_pos;
                break;
            case NIXIE_LG_LEFT:
                g->nodes[i].x = total_primary - primary_pos - psize;
                g->nodes[i].y = cross_pos;
                break;
        }
    }

    free(primary_extent);
    free(layer_sizes);
    free(layer_offset);
    free(layer_cross_extent);
}

void nixie_lg_layout(nixie_lg_graph_t *g) {
    nixie_lg_break_cycles(g);
    nixie_lg_assign_layers(g);
    nixie_lg_reduce_crossings(g, 4);
    nixie_lg_assign_coordinates(g);
}

/* ==========================================================================
 * Edge routing
 * ========================================================================== */

nixie_lg_points_t nixie_lg_route_edge(
    nixie_arena_t *arena, const nixie_lg_graph_t *g, const nixie_lg_edge_t *e) {
    nixie_lg_points_t result;
    result.points = NULL;
    result.count = 0;

    if (e->from < 0 || e->to < 0 ||
        (size_t)e->from >= g->node_count || (size_t)e->to >= g->node_count) {
        return result;
    }

    const nixie_lg_node_t *src = &g->nodes[e->from];
    const nixie_lg_node_t *dst = &g->nodes[e->to];

    if (e->from == e->to) {
        /* Self-loop: small elbow above the node. */
        double x1 = src->x + src->width * 0.75;
        double x2 = src->x + src->width * 0.25;
        double y0 = src->y;
        double yloop = src->y - 20.0;

        nixie_point_t *pts = (nixie_point_t *)nixie_arena_alloc(arena, 4 * sizeof(nixie_point_t));
        pts[0].x = x1; pts[0].y = y0;
        pts[1].x = x1; pts[1].y = yloop;
        pts[2].x = x2; pts[2].y = yloop;
        pts[3].x = x2; pts[3].y = y0;

        result.points = pts;
        result.count = 4;
        return result;
    }

    /* An edge that doesn't connect adjacent layers -- a genuine layer-skip,
     * or a back edge nixie_lg_break_cycles() had to reverse to break a
     * cycle -- can't use the direct exit/elbow/entry routing below: a
     * straight run between its exit and entry points would cut straight
     * through every node sitting in the layers between them (and through
     * any of their own edges). Route it via a dedicated channel around the
     * margin instead (right of everything, for vertical-primary
     * directions; below everything, for horizontal-primary ones). Each
     * such edge gets its own channel, offset by how many earlier edges in
     * g->edges also needed one, so multiple don't overlap each other. */
    int layer_diff = g->nodes[e->to].layer - g->nodes[e->from].layer;
    if (layer_diff < 0) layer_diff = -layer_diff;

    int horizontal_primary_check = (g->direction == NIXIE_LG_LEFT || g->direction == NIXIE_LG_RIGHT);

    if (layer_diff != 1) {
        size_t edge_index = (size_t)(e - g->edges);
        size_t channel = 0;
        for (size_t i = 0; i < edge_index; i++) {
            const nixie_lg_edge_t *other = &g->edges[i];
            if (other->from == other->to) continue;
            int od = g->nodes[other->to].layer - g->nodes[other->from].layer;
            if (od < 0) od = -od;
            if (od != 1) channel++;
        }

        double margin_gap = 30.0;
        nixie_point_t *pts = (nixie_point_t *)nixie_arena_alloc(arena, 4 * sizeof(nixie_point_t));

        if (!horizontal_primary_check) {
            double max_right = 0.0;
            for (size_t i = 0; i < g->node_count; i++) {
                double r = g->nodes[i].x + g->nodes[i].width;
                if (r > max_right) max_right = r;
            }
            double side_x = max_right + margin_gap + (double)channel * margin_gap;
            double ey = src->y + src->height / 2.0;
            double ny = dst->y + dst->height / 2.0;
            pts[0].x = src->x + src->width; pts[0].y = ey;
            pts[1].x = side_x; pts[1].y = ey;
            pts[2].x = side_x; pts[2].y = ny;
            pts[3].x = dst->x + dst->width; pts[3].y = ny;
        } else {
            double max_bottom = 0.0;
            for (size_t i = 0; i < g->node_count; i++) {
                double b = g->nodes[i].y + g->nodes[i].height;
                if (b > max_bottom) max_bottom = b;
            }
            double side_y = max_bottom + margin_gap + (double)channel * margin_gap;
            double ex = src->x + src->width / 2.0;
            double nx = dst->x + dst->width / 2.0;
            pts[0].x = ex; pts[0].y = src->y + src->height;
            pts[1].x = ex; pts[1].y = side_y;
            pts[2].x = nx; pts[2].y = side_y;
            pts[3].x = nx; pts[3].y = dst->y + dst->height;
        }

        result.points = pts;
        result.count = 4;
        return result;
    }

    double scx = src->x + src->width / 2.0;
    double scy = src->y + src->height / 2.0;
    double tcx = dst->x + dst->width / 2.0;
    double tcy = dst->y + dst->height / 2.0;

    int horizontal_primary = (g->direction == NIXIE_LG_LEFT || g->direction == NIXIE_LG_RIGHT);

    nixie_point_t exit_pt, entry_pt;

    if (!horizontal_primary) {
        if (tcy >= scy) {
            exit_pt.x = scx; exit_pt.y = src->y + src->height;
            entry_pt.x = tcx; entry_pt.y = dst->y;
        } else {
            exit_pt.x = scx; exit_pt.y = src->y;
            entry_pt.x = tcx; entry_pt.y = dst->y + dst->height;
        }

        if (fabs(exit_pt.x - entry_pt.x) < 0.01) {
            nixie_point_t *pts = (nixie_point_t *)nixie_arena_alloc(arena, 2 * sizeof(nixie_point_t));
            pts[0] = exit_pt;
            pts[1] = entry_pt;
            result.points = pts;
            result.count = 2;
            return result;
        }

        double mid_y = (exit_pt.y + entry_pt.y) / 2.0;
        nixie_point_t *pts = (nixie_point_t *)nixie_arena_alloc(arena, 4 * sizeof(nixie_point_t));
        pts[0] = exit_pt;
        pts[1].x = exit_pt.x; pts[1].y = mid_y;
        pts[2].x = entry_pt.x; pts[2].y = mid_y;
        pts[3] = entry_pt;
        result.points = pts;
        result.count = 4;
        return result;
    } else {
        if (tcx >= scx) {
            exit_pt.x = src->x + src->width; exit_pt.y = scy;
            entry_pt.x = dst->x; entry_pt.y = tcy;
        } else {
            exit_pt.x = src->x; exit_pt.y = scy;
            entry_pt.x = dst->x + dst->width; entry_pt.y = tcy;
        }

        if (fabs(exit_pt.y - entry_pt.y) < 0.01) {
            nixie_point_t *pts = (nixie_point_t *)nixie_arena_alloc(arena, 2 * sizeof(nixie_point_t));
            pts[0] = exit_pt;
            pts[1] = entry_pt;
            result.points = pts;
            result.count = 2;
            return result;
        }

        double mid_x = (exit_pt.x + entry_pt.x) / 2.0;
        nixie_point_t *pts = (nixie_point_t *)nixie_arena_alloc(arena, 4 * sizeof(nixie_point_t));
        pts[0] = exit_pt;
        pts[1].x = mid_x; pts[1].y = exit_pt.y;
        pts[2].x = mid_x; pts[2].y = entry_pt.y;
        pts[3] = entry_pt;
        result.points = pts;
        result.count = 4;
        return result;
    }
}
