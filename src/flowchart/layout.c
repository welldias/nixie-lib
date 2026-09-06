#include "layout.h"

#include <math.h>
#include <string.h>

#include "../layout_layered.h"
#include "../text_metrics.h"

/* Node label font settings, ported verbatim from
 * beautiful-mermaid/src/styles.ts's FONT_SIZES.nodeLabel /
 * FONT_WEIGHTS.nodeLabel, and NODE_PADDING. */
#define FONT_SIZE_NODE_LABEL 13.0
#define FONT_WEIGHT_NODE_LABEL 500
#define NODE_PADDING_HORIZONTAL 20.0
#define NODE_PADDING_VERTICAL 10.0
#define NODE_PADDING_DIAMOND_EXTRA 24.0

/* Ports beautiful-mermaid/src/layout-engine.ts's estimateNodeSize() verbatim
 * (aside from delegating multi-line text measurement to
 * nixie_measure_multiline instead of the JS measureMultilineText). */
static void estimate_node_size(nixie_arena_t *arena, const char *label, nixie_node_shape_t shape, double *out_w, double *out_h) {
    nixie_multiline_metrics_t metrics = nixie_measure_multiline(arena, label, FONT_SIZE_NODE_LABEL, FONT_WEIGHT_NODE_LABEL);

    double width  = metrics.width + NODE_PADDING_HORIZONTAL * 2;
    double height = metrics.height + NODE_PADDING_VERTICAL * 2;

    if (shape == NIXIE_SHAPE_DIAMOND) {
        double side = (width > height ? width : height) + NODE_PADDING_DIAMOND_EXTRA;
        width       = side;
        height      = side;
    }

    if (shape == NIXIE_SHAPE_CIRCLE || shape == NIXIE_SHAPE_DOUBLECIRCLE) {
        double diameter = ceil(sqrt(width * width + height * height)) + 8.0;
        width           = (shape == NIXIE_SHAPE_DOUBLECIRCLE) ? diameter + 12.0 : diameter;
        height          = width;
    }

    if (shape == NIXIE_SHAPE_HEXAGON) {
        width += NODE_PADDING_HORIZONTAL;
    }

    if (shape == NIXIE_SHAPE_TRAPEZOID || shape == NIXIE_SHAPE_TRAPEZOID_ALT) {
        width += NODE_PADDING_HORIZONTAL;
    }

    if (shape == NIXIE_SHAPE_ASYMMETRIC) {
        width += 12.0;
    }

    if (shape == NIXIE_SHAPE_CYLINDER) {
        height += 14.0;
    }

    if (shape == NIXIE_SHAPE_STATE_START || shape == NIXIE_SHAPE_STATE_END) {
        *out_w = 28.0;
        *out_h = 28.0;
        return;
    }

    width  = width < 60.0 ? 60.0 : width;
    height = height < 36.0 ? 36.0 : height;

    *out_w = width;
    *out_h = height;
}

static nixie_lg_direction_t to_lg_direction(nixie_direction_t d) {
    switch (d) {
    case NIXIE_DIR_TD:
        return NIXIE_LG_DOWN;
    case NIXIE_DIR_BT:
        return NIXIE_LG_UP;
    case NIXIE_DIR_LR:
        return NIXIE_LG_RIGHT;
    case NIXIE_DIR_RL:
        return NIXIE_LG_LEFT;
    default:
        return NIXIE_LG_DOWN;
    }
}

/* ==========================================================================
 * Diamond edge-endpoint clipping -- ports the diamond case of
 * beautiful-mermaid/src/shape-clipping.ts's clipEdgeToShape()/clipToDiamond().
 * Other non-rectangular shapes fall back to the layout engine's bounding-box
 * attachment points, as the plan scopes for v1.
 * ========================================================================== */

static int intersect_horizontal_ray(double ray_y, nixie_point_t p1, nixie_point_t p2, nixie_point_t *out) {
    double dy = p2.y - p1.y;
    if (fabs(dy) < 0.001)
        return 0;
    double t = (ray_y - p1.y) / dy;
    if (t < 0 || t > 1)
        return 0;
    out->x = p1.x + t * (p2.x - p1.x);
    out->y = ray_y;
    return 1;
}

static int intersect_vertical_ray(double ray_x, nixie_point_t p1, nixie_point_t p2, nixie_point_t *out) {
    double dx = p2.x - p1.x;
    if (fabs(dx) < 0.001)
        return 0;
    double t = (ray_x - p1.x) / dx;
    if (t < 0 || t > 1)
        return 0;
    out->y = p1.y + t * (p2.y - p1.y);
    out->x = ray_x;
    return 1;
}

static nixie_point_t clip_to_diamond(nixie_point_t endpoint, nixie_point_t adjacent, nixie_rect_t box) {
    double cx = box.x + box.w / 2.0;
    double cy = box.y + box.h / 2.0;

    nixie_point_t top    = { cx, box.y };
    nixie_point_t right  = { box.x + box.w, cy };
    nixie_point_t bottom = { cx, box.y + box.h };
    nixie_point_t left   = { box.x, cy };

    double dx       = endpoint.x - adjacent.x;
    double dy       = endpoint.y - adjacent.y;
    int is_vertical = fabs(dx) < fabs(dy);

    nixie_point_t out;
    int ok;

    if (is_vertical) {
        double ray_x = endpoint.x;
        if (dy > 0) {
            ok = (ray_x <= cx) ? intersect_vertical_ray(ray_x, left, top, &out) : intersect_vertical_ray(ray_x, top, right, &out);
            if (!ok)
                out = top;
        } else {
            ok = (ray_x <= cx) ? intersect_vertical_ray(ray_x, bottom, left, &out) : intersect_vertical_ray(ray_x, right, bottom, &out);
            if (!ok)
                out = bottom;
        }
    } else {
        double ray_y = endpoint.y;
        if (dx > 0) {
            ok = (ray_y <= cy) ? intersect_horizontal_ray(ray_y, top, left, &out) : intersect_horizontal_ray(ray_y, left, bottom, &out);
            if (!ok)
                out = left;
        } else {
            ok = (ray_y <= cy) ? intersect_horizontal_ray(ray_y, top, right, &out) : intersect_horizontal_ray(ray_y, right, bottom, &out);
            if (!ok)
                out = right;
        }
    }

    return out;
}

static void clip_edge_to_shapes(nixie_point_t *points, size_t count, nixie_rect_t src_box, nixie_node_shape_t src_shape, nixie_rect_t dst_box, nixie_node_shape_t dst_shape) {
    if (count < 2)
        return;

    if (src_shape == NIXIE_SHAPE_DIAMOND) {
        points[0] = clip_to_diamond(points[0], points[1], src_box);
    }
    if (dst_shape == NIXIE_SHAPE_DIAMOND) {
        points[count - 1] = clip_to_diamond(points[count - 1], points[count - 2], dst_box);
    }
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

static const nixie_layout_options_t DEFAULT_OPTIONS = {
    .padding       = 20.0,
    .node_spacing  = 40.0,
    .layer_spacing = 60.0,
};

nixie_positioned_flowchart_t *nixie_flowchart_layout(nixie_arena_t *arena, const nixie_mm_graph_t *graph, const nixie_layout_options_t *opts) {
    if (opts == NULL) {
        opts = &DEFAULT_OPTIONS;
    }

    nixie_positioned_flowchart_t *pf = (nixie_positioned_flowchart_t *)nixie_arena_alloc_zeroed(arena, sizeof(nixie_positioned_flowchart_t));

    pf->direction = graph->direction;

    if (graph->node_count == 0) {
        pf->width  = opts->padding * 2;
        pf->height = opts->padding * 2;
        return pf;
    }

    nixie_lg_graph_t lg;
    lg.node_count    = graph->node_count;
    lg.nodes         = (nixie_lg_node_t *)nixie_arena_alloc_zeroed(arena, lg.node_count * sizeof(nixie_lg_node_t));
    lg.edge_count    = graph->edge_count;
    lg.edges         = graph->edge_count > 0 ? (nixie_lg_edge_t *)nixie_arena_alloc_zeroed(arena, lg.edge_count * sizeof(nixie_lg_edge_t)) : NULL;
    lg.direction     = to_lg_direction(graph->direction);
    lg.node_spacing  = opts->node_spacing;
    lg.layer_spacing = opts->layer_spacing;

    for (size_t i = 0; i < graph->node_count; i++) {
        double w, h;
        estimate_node_size(arena, graph->nodes[i].label, graph->nodes[i].shape, &w, &h);
        lg.nodes[i].width  = w;
        lg.nodes[i].height = h;
    }

    for (size_t i = 0; i < graph->edge_count; i++) {
        lg.edges[i].from = graph->edges[i].source_idx;
        lg.edges[i].to   = graph->edges[i].target_idx;
    }

    nixie_lg_layout(&lg);

    pf->node_count = graph->node_count;
    pf->nodes      = (nixie_pf_node_t *)nixie_arena_alloc(arena, pf->node_count * sizeof(nixie_pf_node_t));
    for (size_t i = 0; i < pf->node_count; i++) {
        pf->nodes[i].index = (int)i;
        pf->nodes[i].id    = graph->nodes[i].id;
        pf->nodes[i].label = graph->nodes[i].label;
        pf->nodes[i].shape = graph->nodes[i].shape;
        pf->nodes[i].x     = lg.nodes[i].x;
        pf->nodes[i].y     = lg.nodes[i].y;
        pf->nodes[i].w     = lg.nodes[i].width;
        pf->nodes[i].h     = lg.nodes[i].height;
        pf->nodes[i].layer = lg.nodes[i].layer;
    }

    pf->edge_count = graph->edge_count;
    pf->edges      = pf->edge_count > 0 ? (nixie_pf_edge_t *)nixie_arena_alloc(arena, pf->edge_count * sizeof(nixie_pf_edge_t)) : NULL;
    for (size_t i = 0; i < pf->edge_count; i++) {
        nixie_lg_points_t route = nixie_lg_route_edge(arena, &lg, &lg.edges[i]);

        int src_idx          = graph->edges[i].source_idx;
        int dst_idx          = graph->edges[i].target_idx;
        nixie_rect_t src_box = { pf->nodes[src_idx].x, pf->nodes[src_idx].y, pf->nodes[src_idx].w, pf->nodes[src_idx].h };
        nixie_rect_t dst_box = { pf->nodes[dst_idx].x, pf->nodes[dst_idx].y, pf->nodes[dst_idx].w, pf->nodes[dst_idx].h };
        clip_edge_to_shapes(route.points, route.count, src_box, pf->nodes[src_idx].shape, dst_box, pf->nodes[dst_idx].shape);

        pf->edges[i].source_idx      = src_idx;
        pf->edges[i].target_idx      = dst_idx;
        pf->edges[i].label           = graph->edges[i].label;
        pf->edges[i].style           = graph->edges[i].style;
        pf->edges[i].has_arrow_start = graph->edges[i].has_arrow_start;
        pf->edges[i].has_arrow_end   = graph->edges[i].has_arrow_end;
        pf->edges[i].points          = route.points;
        pf->edges[i].point_count     = route.count;
        pf->edges[i].has_label_pos   = 0;
    }

    /* Compute the bounding box across nodes and edge points (a self-loop's
     * elbow can extend above its node, and a diamond clip can nudge an
     * endpoint slightly outside the node's own bounding box), then shift
     * everything so the minimum sits exactly at `padding`. */
    double min_x = pf->nodes[0].x, min_y = pf->nodes[0].y;
    double max_x = pf->nodes[0].x + pf->nodes[0].w, max_y = pf->nodes[0].y + pf->nodes[0].h;
    for (size_t i = 1; i < pf->node_count; i++) {
        if (pf->nodes[i].x < min_x)
            min_x = pf->nodes[i].x;
        if (pf->nodes[i].y < min_y)
            min_y = pf->nodes[i].y;
        double nx2 = pf->nodes[i].x + pf->nodes[i].w;
        double ny2 = pf->nodes[i].y + pf->nodes[i].h;
        if (nx2 > max_x)
            max_x = nx2;
        if (ny2 > max_y)
            max_y = ny2;
    }
    for (size_t i = 0; i < pf->edge_count; i++) {
        for (size_t k = 0; k < pf->edges[i].point_count; k++) {
            nixie_point_t p = pf->edges[i].points[k];
            if (p.x < min_x)
                min_x = p.x;
            if (p.y < min_y)
                min_y = p.y;
            if (p.x > max_x)
                max_x = p.x;
            if (p.y > max_y)
                max_y = p.y;
        }
    }

    double shift_x = opts->padding - min_x;
    double shift_y = opts->padding - min_y;

    for (size_t i = 0; i < pf->node_count; i++) {
        pf->nodes[i].x += shift_x;
        pf->nodes[i].y += shift_y;
    }
    for (size_t i = 0; i < pf->edge_count; i++) {
        for (size_t k = 0; k < pf->edges[i].point_count; k++) {
            pf->edges[i].points[k].x += shift_x;
            pf->edges[i].points[k].y += shift_y;
        }
    }

    pf->width  = (max_x - min_x) + opts->padding * 2;
    pf->height = (max_y - min_y) + opts->padding * 2;

    return pf;
}
