#include "layout.h"

#include <string.h>

#include "../layout_layered.h"
#include "../strbuf.h"
#include "../text_metrics.h"

/* Layout constants ported verbatim from beautiful-mermaid/src/er/layout.ts's ER object. */
#define ER_PADDING 40.0
#define ER_BOX_PAD_X2 28.0 /* ER.boxPadX * 2 */
#define ER_HEADER_HEIGHT 34.0
#define ER_ROW_HEIGHT 22.0
#define ER_MIN_WIDTH 140.0
#define ER_ATTR_FONT_SIZE 11.0
#define ER_NODE_SPACING 70.0
#define ER_LAYER_SPACING 90.0

#define FONT_SIZE_NODE_LABEL 13.0
#define FONT_WEIGHT_NODE_LABEL 500

char *nixie_er_attribute_to_string(nixie_arena_t *arena, const nixie_er_attribute_t *attr) {
    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    if (attr->keys != 0) {
        int first = 1;
        if (attr->keys & NIXIE_ER_KEY_PK) {
            nixie_strbuf_append(&sb, "PK");
            first = 0;
        }
        if (attr->keys & NIXIE_ER_KEY_FK) {
            if (!first)
                nixie_strbuf_append_char(&sb, ',');
            nixie_strbuf_append(&sb, "FK");
            first = 0;
        }
        if (attr->keys & NIXIE_ER_KEY_UK) {
            if (!first)
                nixie_strbuf_append_char(&sb, ',');
            nixie_strbuf_append(&sb, "UK");
        }
        nixie_strbuf_append_char(&sb, ' ');
    }
    nixie_strbuf_append(&sb, attr->type);
    nixie_strbuf_append_char(&sb, ' ');
    nixie_strbuf_append(&sb, attr->name);

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

static double estimate_mono_text_width(const char *text, double font_size) {
    return (double)utf8_char_count(text) * font_size * 0.6;
}

nixie_positioned_er_diagram_t *nixie_er_layout(nixie_arena_t *arena, const nixie_er_diagram_t *diagram) {
    nixie_positioned_er_diagram_t *pcd = (nixie_positioned_er_diagram_t *)nixie_arena_alloc_zeroed(arena, sizeof(nixie_positioned_er_diagram_t));

    if (diagram->entity_count == 0) {
        pcd->width  = ER_PADDING * 2;
        pcd->height = ER_PADDING * 2;
        return pcd;
    }

    nixie_lg_graph_t lg;
    lg.node_count    = diagram->entity_count;
    lg.nodes         = (nixie_lg_node_t *)nixie_arena_alloc_zeroed(arena, lg.node_count * sizeof(nixie_lg_node_t));
    lg.edge_count    = diagram->relationship_count;
    lg.edges         = lg.edge_count > 0 ? (nixie_lg_edge_t *)nixie_arena_alloc_zeroed(arena, lg.edge_count * sizeof(nixie_lg_edge_t)) : NULL;
    lg.direction     = NIXIE_LG_RIGHT;
    lg.node_spacing  = ER_NODE_SPACING;
    lg.layer_spacing = ER_LAYER_SPACING;

    for (size_t i = 0; i < diagram->entity_count; i++) {
        const nixie_er_entity_t *entity = &diagram->entities[i];

        double header_text_w = nixie_measure_text_width(entity->label, FONT_SIZE_NODE_LABEL, FONT_WEIGHT_NODE_LABEL);
        double max_attr_w    = 0.0;
        for (size_t k = 0; k < entity->attribute_count; k++) {
            char *s  = nixie_er_attribute_to_string(arena, &entity->attributes[k]);
            double w = estimate_mono_text_width(s, ER_ATTR_FONT_SIZE);
            if (w > max_attr_w)
                max_attr_w = w;
        }

        double width = ER_MIN_WIDTH;
        if (header_text_w + ER_BOX_PAD_X2 > width)
            width = header_text_w + ER_BOX_PAD_X2;
        if (max_attr_w + ER_BOX_PAD_X2 > width)
            width = max_attr_w + ER_BOX_PAD_X2;

        size_t row_count = entity->attribute_count > 1 ? entity->attribute_count : 1;
        double height    = ER_HEADER_HEIGHT + (double)row_count * ER_ROW_HEIGHT;

        lg.nodes[i].width  = width;
        lg.nodes[i].height = height;
    }

    for (size_t i = 0; i < diagram->relationship_count; i++) {
        lg.edges[i].from = diagram->relationships[i].entity1_idx;
        lg.edges[i].to   = diagram->relationships[i].entity2_idx;
    }

    nixie_lg_layout(&lg);

    pcd->node_count = diagram->entity_count;
    pcd->nodes      = (nixie_pe_node_t *)nixie_arena_alloc(arena, pcd->node_count * sizeof(nixie_pe_node_t));
    for (size_t i = 0; i < pcd->node_count; i++) {
        const nixie_er_entity_t *entity = &diagram->entities[i];
        pcd->nodes[i].index             = (int)i;
        pcd->nodes[i].id                = entity->id;
        pcd->nodes[i].label             = entity->label;
        pcd->nodes[i].attributes        = entity->attributes;
        pcd->nodes[i].attribute_count   = entity->attribute_count;
        pcd->nodes[i].x                 = lg.nodes[i].x;
        pcd->nodes[i].y                 = lg.nodes[i].y;
        pcd->nodes[i].w                 = lg.nodes[i].width;
        pcd->nodes[i].h                 = lg.nodes[i].height;
        pcd->nodes[i].header_h          = ER_HEADER_HEIGHT;
        pcd->nodes[i].row_h             = ER_ROW_HEIGHT;
        pcd->nodes[i].layer             = lg.nodes[i].layer;
    }

    pcd->relationship_count = diagram->relationship_count;
    pcd->relationships      = pcd->relationship_count > 0 ? (nixie_pe_relationship_t *)nixie_arena_alloc(arena, pcd->relationship_count * sizeof(nixie_pe_relationship_t)) : NULL;
    for (size_t i = 0; i < pcd->relationship_count; i++) {
        const nixie_er_relationship_t *rel = &diagram->relationships[i];
        nixie_lg_points_t route            = nixie_lg_route_edge(arena, &lg, &lg.edges[i]);

        pcd->relationships[i].entity1_idx  = rel->entity1_idx;
        pcd->relationships[i].entity2_idx  = rel->entity2_idx;
        pcd->relationships[i].cardinality1 = rel->cardinality1;
        pcd->relationships[i].cardinality2 = rel->cardinality2;
        pcd->relationships[i].label        = rel->label;
        pcd->relationships[i].identifying  = rel->identifying;
        pcd->relationships[i].points       = route.points;
        pcd->relationships[i].point_count  = route.count;
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

    double shift_x = ER_PADDING - min_x;
    double shift_y = ER_PADDING - min_y;
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

    pcd->width  = (max_x - min_x) + ER_PADDING * 2;
    pcd->height = (max_y - min_y) + ER_PADDING * 2;

    return pcd;
}
