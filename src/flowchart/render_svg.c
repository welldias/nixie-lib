#include "render_svg.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../strbuf.h"
#include "../text_metrics.h"

/* Font/spacing constants ported from beautiful-mermaid/src/styles.ts. */
#define FONT_SIZE_NODE_LABEL 13.0
#define FONT_WEIGHT_NODE_LABEL 500
#define FONT_SIZE_EDGE_LABEL 11.0
#define FONT_WEIGHT_EDGE_LABEL 400
#define STROKE_WIDTH_OUTER 1.0
#define STROKE_WIDTH_INNER 0.75
#define STROKE_WIDTH_CONNECTOR 1.0
#define ARROW_HEAD_W 8.0
#define ARROW_HEAD_H 5.0
#define TEXT_BASELINE_SHIFT 0.35

/* ==========================================================================
 * XML escaping
 * ========================================================================== */

static void append_escaped_xml(nixie_strbuf_t *sb, const char *text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        switch (c) {
            case '&': nixie_strbuf_append(sb, "&amp;"); break;
            case '<': nixie_strbuf_append(sb, "&lt;"); break;
            case '>': nixie_strbuf_append(sb, "&gt;"); break;
            case '"': nixie_strbuf_append(sb, "&quot;"); break;
            case '\'': nixie_strbuf_append(sb, "&#39;"); break;
            default: nixie_strbuf_append_char(sb, c); break;
        }
    }
}

static void append_escaped_attr(nixie_strbuf_t *sb, const char *text) {
    if (text == NULL) return;
    for (const char *p = text; *p != '\0'; p++) {
        switch (*p) {
            case '&': nixie_strbuf_append(sb, "&amp;"); break;
            case '"': nixie_strbuf_append(sb, "&quot;"); break;
            case '<': nixie_strbuf_append(sb, "&lt;"); break;
            case '>': nixie_strbuf_append(sb, "&gt;"); break;
            default: nixie_strbuf_append_char(sb, *p); break;
        }
    }
}

/* ==========================================================================
 * Multi-line text rendering -- a simplified port of
 * beautiful-mermaid/src/multiline-utils.ts's renderMultilineText() without
 * inline <b>/<i>/<u>/<s> formatting (out of scope for this v1 slice; labels
 * are plain-text after parser.c's normalize_label()).
 * ========================================================================== */

static size_t count_lines(const char *text) {
    size_t n = 1;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '\n') n++;
    }
    return n;
}

static void render_multiline_text(
    nixie_strbuf_t *sb, const char *text, double cx, double cy, double font_size, const char *attrs) {
    size_t line_count = count_lines(text);

    if (line_count == 1) {
        double dy = font_size * TEXT_BASELINE_SHIFT;
        nixie_strbuf_appendf(sb, "<text x=\"%g\" y=\"%g\" %s dy=\"%g\">", cx, cy, attrs, dy);
        append_escaped_xml(sb, text, strlen(text));
        nixie_strbuf_append(sb, "</text>");
        return;
    }

    double line_height = font_size * NIXIE_LINE_HEIGHT_RATIO;
    double first_dy = -(((double)line_count - 1.0) / 2.0) * line_height + font_size * TEXT_BASELINE_SHIFT;

    nixie_strbuf_appendf(sb, "<text x=\"%g\" y=\"%g\" %s>", cx, cy, attrs);

    const char *line_start = text;
    size_t idx = 0;
    for (const char *p = text;; p++) {
        if (*p == '\n' || *p == '\0') {
            double dy = idx == 0 ? first_dy : line_height;
            nixie_strbuf_appendf(sb, "<tspan x=\"%g\" dy=\"%g\">", cx, dy);
            append_escaped_xml(sb, line_start, (size_t)(p - line_start));
            nixie_strbuf_append(sb, "</tspan>");
            idx++;
            if (*p == '\0') break;
            line_start = p + 1;
        }
    }

    nixie_strbuf_append(sb, "</text>");
}

/* ==========================================================================
 * Node shapes -- geometry ported directly from
 * beautiful-mermaid/src/renderer.ts's per-shape render functions.
 * ========================================================================== */

static void render_rect(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    nixie_strbuf_appendf(sb,
        "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"0\" ry=\"0\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        b.x, b.y, b.w, b.h, fill, stroke, sw);
}

static void render_rounded_rect(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    nixie_strbuf_appendf(sb,
        "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"6\" ry=\"6\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        b.x, b.y, b.w, b.h, fill, stroke, sw);
}

static void render_stadium(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    double r = b.h / 2.0;
    nixie_strbuf_appendf(sb,
        "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"%g\" ry=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        b.x, b.y, b.w, b.h, r, r, fill, stroke, sw);
}

static void render_circle(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    double cx = b.x + b.w / 2.0, cy = b.y + b.h / 2.0;
    double r = (b.w < b.h ? b.w : b.h) / 2.0;
    nixie_strbuf_appendf(sb, "<circle cx=\"%g\" cy=\"%g\" r=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        cx, cy, r, fill, stroke, sw);
}

static void render_diamond(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    double cx = b.x + b.w / 2.0, cy = b.y + b.h / 2.0;
    double hw = b.w / 2.0, hh = b.h / 2.0;
    nixie_strbuf_appendf(sb,
        "<polygon points=\"%g,%g %g,%g %g,%g %g,%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        cx, cy - hh, cx + hw, cy, cx, cy + hh, cx - hw, cy, fill, stroke, sw);
}

static void render_subroutine(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    double inset = 8.0;
    render_rect(sb, b, fill, stroke, sw);
    nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"%g\" />",
        b.x + inset, b.y, b.x + inset, b.y + b.h, stroke, sw);
    nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"%g\" />",
        b.x + b.w - inset, b.y, b.x + b.w - inset, b.y + b.h, stroke, sw);
}

static void render_double_circle(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    double cx = b.x + b.w / 2.0, cy = b.y + b.h / 2.0;
    double outer_r = (b.w < b.h ? b.w : b.h) / 2.0;
    double inner_r = outer_r - 5.0;
    nixie_strbuf_appendf(sb, "<circle cx=\"%g\" cy=\"%g\" r=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        cx, cy, outer_r, fill, stroke, sw);
    nixie_strbuf_appendf(sb, "<circle cx=\"%g\" cy=\"%g\" r=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        cx, cy, inner_r, fill, stroke, sw);
}

static void render_hexagon(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    double inset = b.h / 4.0;
    nixie_strbuf_appendf(sb,
        "<polygon points=\"%g,%g %g,%g %g,%g %g,%g %g,%g %g,%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        b.x + inset, b.y, b.x + b.w - inset, b.y, b.x + b.w, b.y + b.h / 2.0,
        b.x + b.w - inset, b.y + b.h, b.x + inset, b.y + b.h, b.x, b.y + b.h / 2.0,
        fill, stroke, sw);
}

static void render_cylinder(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    double ry = 7.0;
    double cx = b.x + b.w / 2.0;
    double body_top = b.y + ry;
    double body_h = b.h - 2.0 * ry;

    nixie_strbuf_appendf(sb, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"%s\" stroke=\"none\" />",
        b.x, body_top, b.w, body_h, fill);
    nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"%g\" />",
        b.x, body_top, b.x, body_top + body_h, stroke, sw);
    nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"%g\" />",
        b.x + b.w, body_top, b.x + b.w, body_top + body_h, stroke, sw);
    nixie_strbuf_appendf(sb, "<ellipse cx=\"%g\" cy=\"%g\" rx=\"%g\" ry=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        cx, b.y + b.h - ry, b.w / 2.0, ry, fill, stroke, sw);
    nixie_strbuf_appendf(sb, "<ellipse cx=\"%g\" cy=\"%g\" rx=\"%g\" ry=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        cx, body_top, b.w / 2.0, ry, fill, stroke, sw);
}

static void render_asymmetric(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    double indent = 12.0;
    nixie_strbuf_appendf(sb,
        "<polygon points=\"%g,%g %g,%g %g,%g %g,%g %g,%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        b.x + indent, b.y, b.x + b.w, b.y, b.x + b.w, b.y + b.h,
        b.x + indent, b.y + b.h, b.x, b.y + b.h / 2.0,
        fill, stroke, sw);
}

static void render_trapezoid(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    double inset = b.w * 0.15;
    nixie_strbuf_appendf(sb,
        "<polygon points=\"%g,%g %g,%g %g,%g %g,%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        b.x + inset, b.y, b.x + b.w - inset, b.y, b.x + b.w, b.y + b.h, b.x, b.y + b.h,
        fill, stroke, sw);
}

static void render_trapezoid_alt(nixie_strbuf_t *sb, nixie_rect_t b, const char *fill, const char *stroke, double sw) {
    double inset = b.w * 0.15;
    nixie_strbuf_appendf(sb,
        "<polygon points=\"%g,%g %g,%g %g,%g %g,%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />",
        b.x, b.y, b.x + b.w, b.y, b.x + b.w - inset, b.y + b.h, b.x + inset, b.y + b.h,
        fill, stroke, sw);
}

static void render_state_start(nixie_strbuf_t *sb, nixie_rect_t b, const char *text_color) {
    double cx = b.x + b.w / 2.0, cy = b.y + b.h / 2.0;
    double r = (b.w < b.h ? b.w : b.h) / 2.0 - 2.0;
    nixie_strbuf_appendf(sb, "<circle cx=\"%g\" cy=\"%g\" r=\"%g\" fill=\"%s\" stroke=\"none\" />", cx, cy, r, text_color);
}

static void render_state_end(nixie_strbuf_t *sb, nixie_rect_t b, const char *text_color) {
    double cx = b.x + b.w / 2.0, cy = b.y + b.h / 2.0;
    double outer_r = (b.w < b.h ? b.w : b.h) / 2.0 - 2.0;
    double inner_r = outer_r - 4.0;
    nixie_strbuf_appendf(sb, "<circle cx=\"%g\" cy=\"%g\" r=\"%g\" fill=\"none\" stroke=\"%s\" stroke-width=\"%g\" />",
        cx, cy, outer_r, text_color, STROKE_WIDTH_INNER * 2);
    nixie_strbuf_appendf(sb, "<circle cx=\"%g\" cy=\"%g\" r=\"%g\" fill=\"%s\" stroke=\"none\" />", cx, cy, inner_r, text_color);
}

static void render_node_shape(nixie_strbuf_t *sb, const nixie_pf_node_t *node, const nixie_resolved_colors_t *colors) {
    nixie_rect_t box = {node->x, node->y, node->w, node->h};
    const char *fill = colors->node_fill;
    const char *stroke = colors->node_stroke;

    switch (node->shape) {
        case NIXIE_SHAPE_DIAMOND: render_diamond(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
        case NIXIE_SHAPE_ROUNDED: render_rounded_rect(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
        case NIXIE_SHAPE_STADIUM: render_stadium(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
        case NIXIE_SHAPE_CIRCLE: render_circle(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
        case NIXIE_SHAPE_SUBROUTINE: render_subroutine(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
        case NIXIE_SHAPE_DOUBLECIRCLE: render_double_circle(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
        case NIXIE_SHAPE_HEXAGON: render_hexagon(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
        case NIXIE_SHAPE_CYLINDER: render_cylinder(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
        case NIXIE_SHAPE_ASYMMETRIC: render_asymmetric(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
        case NIXIE_SHAPE_TRAPEZOID: render_trapezoid(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
        case NIXIE_SHAPE_TRAPEZOID_ALT: render_trapezoid_alt(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
        case NIXIE_SHAPE_STATE_START: render_state_start(sb, box, colors->text); break;
        case NIXIE_SHAPE_STATE_END: render_state_end(sb, box, colors->text); break;
        case NIXIE_SHAPE_RECTANGLE:
        default: render_rect(sb, box, fill, stroke, STROKE_WIDTH_INNER); break;
    }
}

static const char *shape_name(nixie_node_shape_t shape) {
    switch (shape) {
        case NIXIE_SHAPE_DIAMOND: return "diamond";
        case NIXIE_SHAPE_ROUNDED: return "rounded";
        case NIXIE_SHAPE_STADIUM: return "stadium";
        case NIXIE_SHAPE_CIRCLE: return "circle";
        case NIXIE_SHAPE_SUBROUTINE: return "subroutine";
        case NIXIE_SHAPE_DOUBLECIRCLE: return "doublecircle";
        case NIXIE_SHAPE_HEXAGON: return "hexagon";
        case NIXIE_SHAPE_CYLINDER: return "cylinder";
        case NIXIE_SHAPE_ASYMMETRIC: return "asymmetric";
        case NIXIE_SHAPE_TRAPEZOID: return "trapezoid";
        case NIXIE_SHAPE_TRAPEZOID_ALT: return "trapezoid-alt";
        case NIXIE_SHAPE_STATE_START: return "state-start";
        case NIXIE_SHAPE_STATE_END: return "state-end";
        case NIXIE_SHAPE_RECTANGLE:
        default: return "rectangle";
    }
}

static void render_node(nixie_strbuf_t *sb, const nixie_pf_node_t *node, const nixie_resolved_colors_t *colors) {
    nixie_strbuf_append(sb, "<g class=\"node\" data-id=\"");
    append_escaped_attr(sb, node->id);
    nixie_strbuf_append(sb, "\" data-label=\"");
    append_escaped_attr(sb, node->label);
    nixie_strbuf_appendf(sb, "\" data-shape=\"%s\">\n", shape_name(node->shape));

    render_node_shape(sb, node, colors);
    nixie_strbuf_append(sb, "\n");

    int has_label = node->label != NULL && node->label[0] != '\0';
    if ((node->shape != NIXIE_SHAPE_STATE_START && node->shape != NIXIE_SHAPE_STATE_END) && has_label) {
        double cx = node->x + node->w / 2.0;
        double cy = node->y + node->h / 2.0;
        char attrs[160];
        snprintf(attrs, sizeof(attrs),
            "text-anchor=\"middle\" font-size=\"%g\" font-weight=\"%d\" fill=\"%s\"",
            FONT_SIZE_NODE_LABEL, FONT_WEIGHT_NODE_LABEL, colors->text);
        render_multiline_text(sb, node->label, cx, cy, FONT_SIZE_NODE_LABEL, attrs);
    }

    nixie_strbuf_append(sb, "\n</g>");
}

/* ==========================================================================
 * Edges
 * ========================================================================== */

static double point_dist(nixie_point_t a, nixie_point_t b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    return sqrt(dx * dx + dy * dy);
}

static nixie_point_t edge_midpoint(const nixie_point_t *points, size_t count) {
    if (count == 0) return (nixie_point_t){0, 0};
    if (count == 1) return points[0];

    double total = 0.0;
    for (size_t i = 1; i < count; i++) {
        total += point_dist(points[i - 1], points[i]);
    }

    double remaining = total / 2.0;
    for (size_t i = 1; i < count; i++) {
        double seg_len = point_dist(points[i - 1], points[i]);
        if (remaining <= seg_len) {
            double t = seg_len > 0 ? remaining / seg_len : 0.0;
            return (nixie_point_t){
                points[i - 1].x + t * (points[i].x - points[i - 1].x),
                points[i - 1].y + t * (points[i].y - points[i - 1].y),
            };
        }
        remaining -= seg_len;
    }

    return points[count - 1];
}

static void render_edge_label(nixie_strbuf_t *sb, const nixie_pf_edge_t *edge, const nixie_resolved_colors_t *colors) {
    nixie_point_t mid = edge->has_label_pos ? edge->label_pos : edge_midpoint(edge->points, edge->point_count);
    double padding = 8.0;

    /* Plain-text width/height estimate (labels carry no inline formatting in v1). */
    double width = nixie_measure_text_width(edge->label, FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_EDGE_LABEL);
    size_t lines = count_lines(edge->label);
    double height = (double)lines * FONT_SIZE_EDGE_LABEL * NIXIE_LINE_HEIGHT_RATIO;

    double bg_w = width + padding * 2;
    double bg_h = height + padding * 2;

    nixie_strbuf_append(sb, "<g class=\"edge-label\">\n");
    nixie_strbuf_appendf(sb,
        "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"2\" ry=\"2\" fill=\"%s\" stroke=\"%s\" stroke-width=\"1\" />\n",
        mid.x - bg_w / 2.0, mid.y - bg_h / 2.0, bg_w, bg_h, colors->bg, colors->inner_stroke);

    char attrs[160];
    snprintf(attrs, sizeof(attrs),
        "text-anchor=\"middle\" font-size=\"%g\" font-weight=\"%d\" fill=\"%s\"",
        FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_EDGE_LABEL, colors->text_sec);
    render_multiline_text(sb, edge->label, mid.x, mid.y, FONT_SIZE_EDGE_LABEL, attrs);

    nixie_strbuf_append(sb, "\n</g>");
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

char *nixie_flowchart_render_svg(
    const nixie_positioned_flowchart_t *pf, const nixie_resolved_colors_t *colors, int transparent) {
    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    nixie_strbuf_appendf(&sb,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %g %g\" width=\"%g\" height=\"%g\"",
        pf->width, pf->height, pf->width, pf->height);
    if (!transparent) {
        nixie_strbuf_appendf(&sb, " style=\"background:%s\"", colors->bg);
    }
    nixie_strbuf_append(&sb, ">\n");
    nixie_strbuf_append(&sb, "<style>text{font-family:'Inter',system-ui,sans-serif;}</style>\n");

    nixie_strbuf_append(&sb, "<defs>\n");
    nixie_strbuf_appendf(&sb,
        "<marker id=\"arrowhead\" markerWidth=\"%g\" markerHeight=\"%g\" refX=\"%g\" refY=\"%g\" orient=\"auto\">"
        "<polygon points=\"0 0, %g %g, 0 %g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"0.75\" stroke-linejoin=\"round\" /></marker>\n",
        ARROW_HEAD_W, ARROW_HEAD_H, ARROW_HEAD_W - 1, ARROW_HEAD_H / 2.0,
        ARROW_HEAD_W, ARROW_HEAD_H / 2.0, ARROW_HEAD_H, colors->arrow, colors->arrow);
    nixie_strbuf_appendf(&sb,
        "<marker id=\"arrowhead-start\" markerWidth=\"%g\" markerHeight=\"%g\" refX=\"1\" refY=\"%g\" orient=\"auto-start-reverse\">"
        "<polygon points=\"%g 0, 0 %g, %g %g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"0.75\" stroke-linejoin=\"round\" /></marker>\n",
        ARROW_HEAD_W, ARROW_HEAD_H, ARROW_HEAD_H / 2.0,
        ARROW_HEAD_W, ARROW_HEAD_H / 2.0, ARROW_HEAD_W, ARROW_HEAD_H, colors->arrow, colors->arrow);
    nixie_strbuf_append(&sb, "</defs>\n");

    for (size_t i = 0; i < pf->edge_count; i++) {
        const nixie_pf_edge_t *edge = &pf->edges[i];
        if (edge->point_count < 2) continue;

        double sw = edge->style == NIXIE_EDGE_THICK ? STROKE_WIDTH_CONNECTOR * 2 : STROKE_WIDTH_CONNECTOR;
        const char *dash = edge->style == NIXIE_EDGE_DOTTED ? " stroke-dasharray=\"4 4\"" : "";

        nixie_strbuf_append(&sb, "<polyline class=\"edge\" points=\"");
        for (size_t k = 0; k < edge->point_count; k++) {
            if (k > 0) nixie_strbuf_append_char(&sb, ' ');
            nixie_strbuf_appendf(&sb, "%g,%g", edge->points[k].x, edge->points[k].y);
        }
        nixie_strbuf_appendf(&sb, "\" fill=\"none\" stroke=\"%s\" stroke-width=\"%g\"%s", colors->line, sw, dash);
        if (edge->has_arrow_end) nixie_strbuf_append(&sb, " marker-end=\"url(#arrowhead)\"");
        if (edge->has_arrow_start) nixie_strbuf_append(&sb, " marker-start=\"url(#arrowhead-start)\"");
        nixie_strbuf_append(&sb, " />\n");
    }

    for (size_t i = 0; i < pf->edge_count; i++) {
        if (pf->edges[i].label != NULL && pf->edges[i].label[0] != '\0') {
            render_edge_label(&sb, &pf->edges[i], colors);
            nixie_strbuf_append(&sb, "\n");
        }
    }

    for (size_t i = 0; i < pf->node_count; i++) {
        render_node(&sb, &pf->nodes[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }

    nixie_strbuf_append(&sb, "</svg>");

    return nixie_strbuf_release(&sb);
}
