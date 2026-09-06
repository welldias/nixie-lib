#include "layout.h"

#include <string.h>

#include "../layout_layered.h"
#include "../strbuf.h"
#include "../text_metrics.h"

/* Layout constants ported verbatim from beautiful-mermaid/src/class/layout.ts's CLS object. */
#define CLS_PADDING 40.0
#define CLS_HEADER_BASE_HEIGHT 32.0
#define CLS_ANNOTATION_HEIGHT 16.0
#define CLS_MEMBER_ROW_HEIGHT 20.0
#define CLS_SECTION_PAD_Y 8.0
#define CLS_EMPTY_SECTION_HEIGHT 8.0
#define CLS_MIN_WIDTH 120.0
#define CLS_MEMBER_FONT_SIZE 11.0
#define CLS_NODE_SPACING 40.0
#define CLS_LAYER_SPACING 60.0
#define CLS_BOX_PAD_X2 16.0 /* CLS.boxPadX * 2 */

#define FONT_SIZE_NODE_LABEL 13.0
#define FONT_WEIGHT_NODE_LABEL 500

char *nixie_class_member_to_string(nixie_arena_t *arena, const nixie_class_member_t *m) {
    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    if (m->visibility != NIXIE_VIS_NONE) {
        char v = '~';
        if (m->visibility == NIXIE_VIS_PUBLIC)
            v = '+';
        else if (m->visibility == NIXIE_VIS_PRIVATE)
            v = '-';
        else if (m->visibility == NIXIE_VIS_PROTECTED)
            v = '#';
        nixie_strbuf_append_char(&sb, v);
        nixie_strbuf_append_char(&sb, ' ');
    }

    if (m->is_method) {
        nixie_strbuf_append(&sb, m->name);
        nixie_strbuf_append_char(&sb, '(');
        if (m->params != NULL)
            nixie_strbuf_append(&sb, m->params);
        nixie_strbuf_append_char(&sb, ')');
    } else {
        nixie_strbuf_append(&sb, m->name);
    }

    if (m->type != NULL) {
        nixie_strbuf_append(&sb, ": ");
        nixie_strbuf_append(&sb, m->type);
    }

    char *result = nixie_arena_strdup(arena, sb.data != NULL ? sb.data : "");
    nixie_strbuf_free(&sb);
    return result;
}

static size_t utf8_char_count(const char *s) {
    size_t count = 0;
    while (*s != '\0') {
        uint32_t cp;
        s += nixie_utf8_decode(s, &cp);
        count++;
    }
    return count;
}

/* Monospace width estimate ported from styles.ts's estimateMonoTextWidth(). */
static double estimate_mono_text_width(const char *text, double font_size) {
    return (double)utf8_char_count(text) * font_size * 0.6;
}

static double max_member_width(nixie_arena_t *arena, const nixie_class_member_t *members, size_t count) {
    double max_w = 0.0;
    for (size_t i = 0; i < count; i++) {
        char *s  = nixie_class_member_to_string(arena, &members[i]);
        double w = estimate_mono_text_width(s, CLS_MEMBER_FONT_SIZE);
        if (w > max_w)
            max_w = w;
    }
    return max_w;
}

nixie_positioned_class_diagram_t *nixie_class_layout(nixie_arena_t *arena, const nixie_class_diagram_t *diagram) {
    nixie_positioned_class_diagram_t *pcd = (nixie_positioned_class_diagram_t *)nixie_arena_alloc_zeroed(arena, sizeof(nixie_positioned_class_diagram_t));

    if (diagram->class_count == 0) {
        pcd->width  = CLS_PADDING * 2;
        pcd->height = CLS_PADDING * 2;
        return pcd;
    }

    nixie_lg_graph_t lg;
    lg.node_count    = diagram->class_count;
    lg.nodes         = (nixie_lg_node_t *)nixie_arena_alloc_zeroed(arena, lg.node_count * sizeof(nixie_lg_node_t));
    lg.edge_count    = diagram->relationship_count;
    lg.edges         = lg.edge_count > 0 ? (nixie_lg_edge_t *)nixie_arena_alloc_zeroed(arena, lg.edge_count * sizeof(nixie_lg_edge_t)) : NULL;
    lg.direction     = NIXIE_LG_DOWN;
    lg.node_spacing  = CLS_NODE_SPACING;
    lg.layer_spacing = CLS_LAYER_SPACING;

    double *header_h = (double *)nixie_arena_alloc(arena, diagram->class_count * sizeof(double));
    double *attr_h   = (double *)nixie_arena_alloc(arena, diagram->class_count * sizeof(double));
    double *method_h = (double *)nixie_arena_alloc(arena, diagram->class_count * sizeof(double));

    for (size_t i = 0; i < diagram->class_count; i++) {
        const nixie_class_node_t *cls = &diagram->classes[i];

        double hh   = cls->annotation != NULL ? CLS_HEADER_BASE_HEIGHT + CLS_ANNOTATION_HEIGHT : CLS_HEADER_BASE_HEIGHT;
        double ah   = cls->attribute_count > 0 ? (double)cls->attribute_count * CLS_MEMBER_ROW_HEIGHT + CLS_SECTION_PAD_Y : CLS_EMPTY_SECTION_HEIGHT;
        double mh   = cls->method_count > 0 ? (double)cls->method_count * CLS_MEMBER_ROW_HEIGHT + CLS_SECTION_PAD_Y : CLS_EMPTY_SECTION_HEIGHT;
        header_h[i] = hh;
        attr_h[i]   = ah;
        method_h[i] = mh;

        double header_text_w = nixie_measure_text_width(cls->label, FONT_SIZE_NODE_LABEL, FONT_WEIGHT_NODE_LABEL);
        double max_attr_w    = max_member_width(arena, cls->attributes, cls->attribute_count);
        double max_method_w  = max_member_width(arena, cls->methods, cls->method_count);

        double width = CLS_MIN_WIDTH;
        if (header_text_w + CLS_BOX_PAD_X2 > width)
            width = header_text_w + CLS_BOX_PAD_X2;
        if (max_attr_w + CLS_BOX_PAD_X2 > width)
            width = max_attr_w + CLS_BOX_PAD_X2;
        if (max_method_w + CLS_BOX_PAD_X2 > width)
            width = max_method_w + CLS_BOX_PAD_X2;

        lg.nodes[i].width  = width;
        lg.nodes[i].height = hh + ah + mh;
    }

    for (size_t i = 0; i < diagram->relationship_count; i++) {
        lg.edges[i].from = diagram->relationships[i].from_idx;
        lg.edges[i].to   = diagram->relationships[i].to_idx;
    }

    nixie_lg_layout(&lg);

    pcd->node_count = diagram->class_count;
    pcd->nodes      = (nixie_pc_node_t *)nixie_arena_alloc(arena, pcd->node_count * sizeof(nixie_pc_node_t));
    for (size_t i = 0; i < pcd->node_count; i++) {
        const nixie_class_node_t *cls = &diagram->classes[i];
        pcd->nodes[i].index           = (int)i;
        pcd->nodes[i].id              = cls->id;
        pcd->nodes[i].label           = cls->label;
        pcd->nodes[i].annotation      = cls->annotation;
        pcd->nodes[i].attributes      = cls->attributes;
        pcd->nodes[i].attribute_count = cls->attribute_count;
        pcd->nodes[i].methods         = cls->methods;
        pcd->nodes[i].method_count    = cls->method_count;
        pcd->nodes[i].x               = lg.nodes[i].x;
        pcd->nodes[i].y               = lg.nodes[i].y;
        pcd->nodes[i].w               = lg.nodes[i].width;
        pcd->nodes[i].h               = lg.nodes[i].height;
        pcd->nodes[i].header_h        = header_h[i];
        pcd->nodes[i].attr_h          = attr_h[i];
        pcd->nodes[i].method_h        = method_h[i];
        pcd->nodes[i].layer           = lg.nodes[i].layer;
    }

    pcd->relationship_count = diagram->relationship_count;
    pcd->relationships      = pcd->relationship_count > 0 ? (nixie_pc_relationship_t *)nixie_arena_alloc(arena, pcd->relationship_count * sizeof(nixie_pc_relationship_t)) : NULL;
    for (size_t i = 0; i < pcd->relationship_count; i++) {
        const nixie_class_relationship_t *rel = &diagram->relationships[i];
        nixie_lg_points_t route               = nixie_lg_route_edge(arena, &lg, &lg.edges[i]);

        pcd->relationships[i].from_idx         = rel->from_idx;
        pcd->relationships[i].to_idx           = rel->to_idx;
        pcd->relationships[i].type             = rel->type;
        pcd->relationships[i].marker_at        = rel->marker_at;
        pcd->relationships[i].label            = rel->label;
        pcd->relationships[i].from_cardinality = rel->from_cardinality;
        pcd->relationships[i].to_cardinality   = rel->to_cardinality;
        pcd->relationships[i].points           = route.points;
        pcd->relationships[i].point_count      = route.count;
    }

    double min_x = pcd->nodes[0].x, min_y = pcd->nodes[0].y;
    double max_x = pcd->nodes[0].x + pcd->nodes[0].w, max_y = pcd->nodes[0].y + pcd->nodes[0].h;
    for (size_t i = 1; i < pcd->node_count; i++) {
        if (pcd->nodes[i].x < min_x)
            min_x = pcd->nodes[i].x;
        if (pcd->nodes[i].y < min_y)
            min_y = pcd->nodes[i].y;
        double nx2 = pcd->nodes[i].x + pcd->nodes[i].w;
        double ny2 = pcd->nodes[i].y + pcd->nodes[i].h;
        if (nx2 > max_x)
            max_x = nx2;
        if (ny2 > max_y)
            max_y = ny2;
    }
    for (size_t i = 0; i < pcd->relationship_count; i++) {
        for (size_t k = 0; k < pcd->relationships[i].point_count; k++) {
            nixie_point_t p = pcd->relationships[i].points[k];
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

    double shift_x = CLS_PADDING - min_x;
    double shift_y = CLS_PADDING - min_y;
    for (size_t i = 0; i < pcd->node_count; i++) {
        pcd->nodes[i].x += shift_x;
        pcd->nodes[i].y += shift_y;
    }
    for (size_t i = 0; i < pcd->relationship_count; i++) {
        for (size_t k = 0; k < pcd->relationships[i].point_count; k++) {
            pcd->relationships[i].points[k].x += shift_x;
            pcd->relationships[i].points[k].y += shift_y;
        }
    }

    pcd->width  = (max_x - min_x) + CLS_PADDING * 2;
    pcd->height = (max_y - min_y) + CLS_PADDING * 2;

    return pcd;
}
