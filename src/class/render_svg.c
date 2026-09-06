#include "render_svg.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../strbuf.h"
#include "../text_metrics.h"

/* ==========================================================================
 * XML escaping / multi-line text (small local copies of
 * flowchart/render_svg.c's own helpers -- see that file's note on why these
 * aren't factored into a shared module).
 * ========================================================================== */

static void append_escaped_xml(nixie_strbuf_t *sb, const char *text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        switch (c) {
        case '&':
            nixie_strbuf_append(sb, "&amp;");
            break;
        case '<':
            nixie_strbuf_append(sb, "&lt;");
            break;
        case '>':
            nixie_strbuf_append(sb, "&gt;");
            break;
        case '"':
            nixie_strbuf_append(sb, "&quot;");
            break;
        case '\'':
            nixie_strbuf_append(sb, "&#39;");
            break;
        default:
            nixie_strbuf_append_char(sb, c);
            break;
        }
    }
}

static void append_escaped_attr(nixie_strbuf_t *sb, const char *text) {
    if (text == NULL)
        return;
    for (const char *p = text; *p != '\0'; p++) {
        switch (*p) {
        case '&':
            nixie_strbuf_append(sb, "&amp;");
            break;
        case '"':
            nixie_strbuf_append(sb, "&quot;");
            break;
        case '<':
            nixie_strbuf_append(sb, "&lt;");
            break;
        case '>':
            nixie_strbuf_append(sb, "&gt;");
            break;
        default:
            nixie_strbuf_append_char(sb, *p);
            break;
        }
    }
}

static size_t count_lines(const char *text) {
    size_t n = 1;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '\n')
            n++;
    }
    return n;
}

static void render_multiline_text(nixie_strbuf_t *sb, const char *text, double cx, double cy, double font_size, const char *attrs) {
    size_t line_count = count_lines(text);

    if (line_count == 1) {
        double dy = font_size * 0.35;
        nixie_strbuf_appendf(sb, "<text x=\"%g\" y=\"%g\" %s dy=\"%g\">", cx, cy, attrs, dy);
        append_escaped_xml(sb, text, strlen(text));
        nixie_strbuf_append(sb, "</text>");
        return;
    }

    double line_height = font_size * NIXIE_LINE_HEIGHT_RATIO;
    double first_dy    = -(((double)line_count - 1.0) / 2.0) * line_height + font_size * 0.35;

    nixie_strbuf_appendf(sb, "<text x=\"%g\" y=\"%g\" %s>", cx, cy, attrs);

    const char *line_start = text;
    size_t idx             = 0;
    for (const char *p = text;; p++) {
        if (*p == '\n' || *p == '\0') {
            double dy = idx == 0 ? first_dy : line_height;
            nixie_strbuf_appendf(sb, "<tspan x=\"%g\" dy=\"%g\">", cx, dy);
            append_escaped_xml(sb, line_start, (size_t)(p - line_start));
            nixie_strbuf_append(sb, "</tspan>");
            idx++;
            if (*p == '\0')
                break;
            line_start = p + 1;
        }
    }

    nixie_strbuf_append(sb, "</text>");
}

/* ==========================================================================
 * Relationship markers
 * ========================================================================== */

static void append_marker_defs(nixie_strbuf_t *sb, const nixie_resolved_colors_t *colors) {
    nixie_strbuf_appendf(sb,
        "<marker id=\"cls-inherit\" markerWidth=\"12\" markerHeight=\"10\" refX=\"12\" refY=\"5\" orient=\"auto-start-reverse\">"
        "<polygon points=\"0 0, 12 5, 0 10\" fill=\"%s\" stroke=\"%s\" stroke-width=\"1.5\" /></marker>\n",
        colors->bg, colors->arrow);
    nixie_strbuf_appendf(sb,
        "<marker id=\"cls-composition\" markerWidth=\"12\" markerHeight=\"10\" refX=\"0\" refY=\"5\" orient=\"auto-start-reverse\">"
        "<polygon points=\"6 0, 12 5, 6 10, 0 5\" fill=\"%s\" stroke=\"%s\" stroke-width=\"1\" /></marker>\n",
        colors->arrow, colors->arrow);
    nixie_strbuf_appendf(sb,
        "<marker id=\"cls-aggregation\" markerWidth=\"12\" markerHeight=\"10\" refX=\"0\" refY=\"5\" orient=\"auto-start-reverse\">"
        "<polygon points=\"6 0, 12 5, 6 10, 0 5\" fill=\"%s\" stroke=\"%s\" stroke-width=\"1.5\" /></marker>\n",
        colors->bg, colors->arrow);
    nixie_strbuf_appendf(sb,
        "<marker id=\"cls-arrow\" markerWidth=\"8\" markerHeight=\"6\" refX=\"8\" refY=\"3\" orient=\"auto-start-reverse\">"
        "<polyline points=\"0 0, 8 3, 0 6\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.5\" /></marker>\n",
        colors->arrow);
}

static const char *marker_id_for(nixie_relationship_type_t type) {
    switch (type) {
    case NIXIE_REL_INHERITANCE:
    case NIXIE_REL_REALIZATION:
        return "cls-inherit";
    case NIXIE_REL_COMPOSITION:
        return "cls-composition";
    case NIXIE_REL_AGGREGATION:
        return "cls-aggregation";
    case NIXIE_REL_ASSOCIATION:
    case NIXIE_REL_DEPENDENCY:
    default:
        return "cls-arrow";
    }
}

/* ==========================================================================
 * Relationship rendering
 * ========================================================================== */

static void render_relationship(nixie_strbuf_t *sb, const nixie_pc_relationship_t *rel, const nixie_resolved_colors_t *colors) {
    if (rel->point_count < 2)
        return;

    int dashed            = (rel->type == NIXIE_REL_DEPENDENCY || rel->type == NIXIE_REL_REALIZATION);
    const char *marker_id = marker_id_for(rel->type);

    nixie_strbuf_append(sb, "<polyline class=\"class-relationship\" points=\"");
    for (size_t i = 0; i < rel->point_count; i++) {
        if (i > 0)
            nixie_strbuf_append_char(sb, ' ');
        nixie_strbuf_appendf(sb, "%g,%g", rel->points[i].x, rel->points[i].y);
    }
    nixie_strbuf_appendf(sb, "\" fill=\"none\" stroke=\"%s\" stroke-width=\"1\"%s", colors->line, dashed ? " stroke-dasharray=\"6 4\"" : "");
    if (rel->marker_at == NIXIE_MARKER_FROM) {
        nixie_strbuf_appendf(sb, " marker-start=\"url(#%s)\"", marker_id);
    } else {
        nixie_strbuf_appendf(sb, " marker-end=\"url(#%s)\"", marker_id);
    }
    nixie_strbuf_append(sb, " />");
}

static nixie_point_t rel_midpoint(const nixie_point_t *points, size_t count) {
    if (count == 0) {
        nixie_point_t zero = { 0, 0 };
        return zero;
    }
    return points[count / 2];
}

static nixie_point_t cardinality_offset(nixie_point_t from, nixie_point_t to) {
    double dx = to.x - from.x, dy = to.y - from.y;
    nixie_point_t off;
    if (fabs(dx) > fabs(dy)) {
        off.x = dx > 0 ? 14 : -14;
        off.y = -10;
    } else {
        off.x = -14;
        off.y = dy > 0 ? 14 : -14;
    }
    return off;
}

static void render_relationship_labels(nixie_strbuf_t *sb, const nixie_pc_relationship_t *rel, const nixie_resolved_colors_t *colors) {
    if (rel->label == NULL && rel->from_cardinality == NULL && rel->to_cardinality == NULL)
        return;
    if (rel->point_count < 2)
        return;

    char attrs[160];
    snprintf(attrs, sizeof(attrs), "font-size=\"11\" text-anchor=\"middle\" font-weight=\"400\" fill=\"%s\"", colors->text_muted);

    if (rel->label != NULL) {
        nixie_point_t pos = rel_midpoint(rel->points, rel->point_count);
        render_multiline_text(sb, rel->label, pos.x, pos.y - 8, 11.0, attrs);
        nixie_strbuf_append(sb, "\n");
    }
    if (rel->from_cardinality != NULL) {
        nixie_point_t p    = rel->points[0];
        nixie_point_t next = rel->points[1];
        nixie_point_t off  = cardinality_offset(p, next);
        render_multiline_text(sb, rel->from_cardinality, p.x + off.x, p.y + off.y, 11.0, attrs);
        nixie_strbuf_append(sb, "\n");
    }
    if (rel->to_cardinality != NULL) {
        nixie_point_t p    = rel->points[rel->point_count - 1];
        nixie_point_t prev = rel->points[rel->point_count - 2];
        nixie_point_t off  = cardinality_offset(p, prev);
        render_multiline_text(sb, rel->to_cardinality, p.x + off.x, p.y + off.y, 11.0, attrs);
        nixie_strbuf_append(sb, "\n");
    }
}

/* ==========================================================================
 * Class box rendering
 * ========================================================================== */

static char visibility_char(nixie_visibility_t v) {
    switch (v) {
    case NIXIE_VIS_PUBLIC:
        return '+';
    case NIXIE_VIS_PRIVATE:
        return '-';
    case NIXIE_VIS_PROTECTED:
        return '#';
    case NIXIE_VIS_PACKAGE:
        return '~';
    default:
        return '\0';
    }
}

static void render_member_row(nixie_strbuf_t *sb, const nixie_class_member_t *m, double x, double y, const nixie_resolved_colors_t *colors) {
    const char *font_style = m->is_abstract ? " font-style=\"italic\"" : "";
    const char *decoration = m->is_static ? " text-decoration=\"underline\"" : "";

    nixie_strbuf_appendf(sb, "<text x=\"%g\" y=\"%g\" class=\"mono\" dy=\"0.35em\" font-size=\"11\" font-weight=\"400\"%s%s>", x, y, font_style, decoration);

    char vis = visibility_char(m->visibility);
    if (vis != '\0') {
        nixie_strbuf_appendf(sb, "<tspan fill=\"%s\">%c </tspan>", colors->text_faint, vis);
    }

    nixie_strbuf_appendf(sb, "<tspan fill=\"%s\">", colors->text_sec);
    append_escaped_xml(sb, m->name, strlen(m->name));
    if (m->is_method) {
        nixie_strbuf_append_char(sb, '(');
        if (m->params != NULL)
            append_escaped_xml(sb, m->params, strlen(m->params));
        nixie_strbuf_append_char(sb, ')');
    }
    nixie_strbuf_append(sb, "</tspan>");

    if (m->type != NULL) {
        nixie_strbuf_appendf(sb, "<tspan fill=\"%s\">: </tspan>", colors->text_faint);
        nixie_strbuf_appendf(sb, "<tspan fill=\"%s\">", colors->text_muted);
        append_escaped_xml(sb, m->type, strlen(m->type));
        nixie_strbuf_append(sb, "</tspan>");
    }

    nixie_strbuf_append(sb, "</text>");
}

static void render_class_box(nixie_strbuf_t *sb, const nixie_pc_node_t *cls, const nixie_resolved_colors_t *colors) {
    double x = cls->x, y = cls->y, w = cls->w, h = cls->h;

    nixie_strbuf_append(sb, "<g class=\"class-node\" data-id=\"");
    append_escaped_attr(sb, cls->id);
    nixie_strbuf_append(sb, "\" data-label=\"");
    append_escaped_attr(sb, cls->label);
    if (cls->annotation != NULL) {
        nixie_strbuf_append(sb, "\" data-annotation=\"");
        append_escaped_attr(sb, cls->annotation);
    }
    nixie_strbuf_append(sb, "\">\n");

    nixie_strbuf_appendf(sb, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"1\" />\n", x, y, w, h, colors->node_fill, colors->node_stroke);
    nixie_strbuf_appendf(sb, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"1\" />\n", x, y, w, cls->header_h, colors->group_hdr, colors->node_stroke);

    double name_y = y + cls->header_h / 2.0;
    if (cls->annotation != NULL) {
        double annot_y = y + 12.0;
        nixie_strbuf_appendf(sb,
            "<text x=\"%g\" y=\"%g\" text-anchor=\"middle\" dy=\"0.35em\" font-size=\"10\" font-weight=\"500\" "
            "font-style=\"italic\" fill=\"%s\">&lt;&lt;",
            x + w / 2.0, annot_y, colors->text_muted);
        append_escaped_xml(sb, cls->annotation, strlen(cls->annotation));
        nixie_strbuf_append(sb, "&gt;&gt;</text>\n");
        name_y = y + cls->header_h / 2.0 + 6.0;
    }

    char name_attrs[160];
    snprintf(name_attrs, sizeof(name_attrs), "text-anchor=\"middle\" font-size=\"13\" font-weight=\"700\" fill=\"%s\"", colors->text);
    render_multiline_text(sb, cls->label, x + w / 2.0, name_y, 13.0, name_attrs);
    nixie_strbuf_append(sb, "\n");

    double attr_top = y + cls->header_h;
    nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"0.75\" />\n", x, attr_top, x + w, attr_top, colors->node_stroke);

    for (size_t i = 0; i < cls->attribute_count; i++) {
        double member_y = attr_top + 4.0 + (double)i * 20.0 + 10.0;
        render_member_row(sb, &cls->attributes[i], x + 8.0, member_y, colors);
        nixie_strbuf_append(sb, "\n");
    }

    double method_top = attr_top + cls->attr_h;
    nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"0.75\" />\n", x, method_top, x + w, method_top, colors->node_stroke);

    for (size_t i = 0; i < cls->method_count; i++) {
        double member_y = method_top + 4.0 + (double)i * 20.0 + 10.0;
        render_member_row(sb, &cls->methods[i], x + 8.0, member_y, colors);
        nixie_strbuf_append(sb, "\n");
    }

    nixie_strbuf_append(sb, "</g>");
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

char *nixie_class_render_svg(const nixie_positioned_class_diagram_t *pcd, const nixie_resolved_colors_t *colors, int transparent) {
    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    nixie_strbuf_appendf(&sb, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %g %g\" width=\"%g\" height=\"%g\"", pcd->width, pcd->height, pcd->width, pcd->height);
    if (!transparent) {
        nixie_strbuf_appendf(&sb, " style=\"background:%s\"", colors->bg);
    }
    nixie_strbuf_append(&sb, ">\n");
    nixie_strbuf_append(&sb, "<style>text{font-family:'Inter',system-ui,sans-serif;}.mono{font-family:'JetBrains Mono','SF Mono',monospace;}</style>\n");

    nixie_strbuf_append(&sb, "<defs>\n");
    append_marker_defs(&sb, colors);
    nixie_strbuf_append(&sb, "</defs>\n");

    for (size_t i = 0; i < pcd->relationship_count; i++) {
        render_relationship(&sb, &pcd->relationships[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }

    for (size_t i = 0; i < pcd->node_count; i++) {
        render_class_box(&sb, &pcd->nodes[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }

    for (size_t i = 0; i < pcd->relationship_count; i++) {
        render_relationship_labels(&sb, &pcd->relationships[i], colors);
    }

    nixie_strbuf_append(&sb, "</svg>");

    return nixie_strbuf_release(&sb);
}
