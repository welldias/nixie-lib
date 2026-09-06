#include "render_svg.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../strbuf.h"
#include "../text_metrics.h"

/* ==========================================================================
 * XML escaping / multi-line text (small local copies -- see
 * flowchart/render_svg.c's note on why these aren't factored into a shared
 * module)
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
 * Entity box rendering
 * ========================================================================== */

static void build_key_text(const nixie_er_attribute_t *attr, char *buf, size_t buflen) {
    buf[0]    = '\0';
    int first = 1;
    if (attr->keys & NIXIE_ER_KEY_PK) {
        strncat(buf, "PK", buflen - strlen(buf) - 1);
        first = 0;
    }
    if (attr->keys & NIXIE_ER_KEY_FK) {
        if (!first)
            strncat(buf, ",", buflen - strlen(buf) - 1);
        strncat(buf, "FK", buflen - strlen(buf) - 1);
        first = 0;
    }
    if (attr->keys & NIXIE_ER_KEY_UK) {
        if (!first)
            strncat(buf, ",", buflen - strlen(buf) - 1);
        strncat(buf, "UK", buflen - strlen(buf) - 1);
    }
}

static void render_attribute_row(nixie_strbuf_t *sb, const nixie_er_attribute_t *attr, double box_x, double y, double box_width, const nixie_resolved_colors_t *colors) {
    int has_comment = attr->comment != NULL && attr->comment[0] != '\0';
    if (has_comment) {
        nixie_strbuf_append(sb, "<g><title>");
        append_escaped_xml(sb, attr->comment, strlen(attr->comment));
        nixie_strbuf_append(sb, "</title>");
    }

    double key_width = 0.0;
    if (attr->keys != 0) {
        char key_text[16];
        build_key_text(attr, key_text, sizeof(key_text));
        key_width = nixie_measure_text_width(key_text, 9.0, 600) + 8.0;

        nixie_strbuf_appendf(sb, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"14\" rx=\"2\" ry=\"2\" fill=\"%s\" />", box_x + 6, y - 7, key_width, colors->key_badge);
        nixie_strbuf_appendf(sb, "<text x=\"%g\" y=\"%g\" text-anchor=\"middle\" dy=\"0.35em\" font-size=\"9\" font-weight=\"600\" fill=\"%s\">", box_x + 6 + key_width / 2.0, y, colors->text_sec);
        append_escaped_xml(sb, key_text, strlen(key_text));
        nixie_strbuf_append(sb, "</text>");
    }

    double type_x = box_x + 8.0 + (key_width > 0.0 ? key_width + 6.0 : 0.0);
    nixie_strbuf_appendf(sb, "<text x=\"%g\" y=\"%g\" class=\"mono\" dy=\"0.35em\" font-size=\"11\" font-weight=\"400\"><tspan fill=\"%s\">", type_x, y, colors->text_muted);
    append_escaped_xml(sb, attr->type, strlen(attr->type));
    nixie_strbuf_append(sb, "</tspan></text>");

    double name_x = box_x + box_width - 8.0;
    nixie_strbuf_appendf(sb, "<text x=\"%g\" y=\"%g\" class=\"mono\" text-anchor=\"end\" dy=\"0.35em\" font-size=\"11\" font-weight=\"400\"><tspan fill=\"%s\">", name_x, y, colors->text_sec);
    append_escaped_xml(sb, attr->name, strlen(attr->name));
    nixie_strbuf_append(sb, "</tspan></text>");

    if (has_comment)
        nixie_strbuf_append(sb, "</g>");
}

static void render_entity_box(nixie_strbuf_t *sb, const nixie_pe_node_t *entity, const nixie_resolved_colors_t *colors) {
    double x = entity->x, y = entity->y, w = entity->w, h = entity->h;

    nixie_strbuf_append(sb, "<g class=\"entity\" data-id=\"");
    append_escaped_attr(sb, entity->id);
    nixie_strbuf_append(sb, "\" data-label=\"");
    append_escaped_attr(sb, entity->label);
    nixie_strbuf_append(sb, "\">\n");

    nixie_strbuf_appendf(sb, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"1\" />\n", x, y, w, h, colors->node_fill, colors->node_stroke);
    nixie_strbuf_appendf(sb, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"1\" />\n", x, y, w, entity->header_h, colors->group_hdr, colors->node_stroke);

    char name_attrs[160];
    snprintf(name_attrs, sizeof(name_attrs), "text-anchor=\"middle\" font-size=\"13\" font-weight=\"700\" fill=\"%s\"", colors->text);
    render_multiline_text(sb, entity->label, x + w / 2.0, y + entity->header_h / 2.0, 13.0, name_attrs);
    nixie_strbuf_append(sb, "\n");

    double attr_top = y + entity->header_h;
    nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"0.75\" />\n", x, attr_top, x + w, attr_top, colors->node_stroke);

    for (size_t i = 0; i < entity->attribute_count; i++) {
        double row_y = attr_top + (double)i * entity->row_h + entity->row_h / 2.0;
        render_attribute_row(sb, &entity->attributes[i], x, row_y, w, colors);
        nixie_strbuf_append(sb, "\n");
    }
    if (entity->attribute_count == 0) {
        nixie_strbuf_appendf(sb, "<text x=\"%g\" y=\"%g\" text-anchor=\"middle\" dy=\"0.35em\" font-size=\"11\" fill=\"%s\" font-style=\"italic\">(no attributes)</text>\n", x + w / 2.0, attr_top + entity->row_h / 2.0, colors->text_faint);
    }

    nixie_strbuf_append(sb, "</g>");
}

/* ==========================================================================
 * Relationship rendering
 * ========================================================================== */

static void render_relationship_line(nixie_strbuf_t *sb, const nixie_pe_relationship_t *rel, const nixie_resolved_colors_t *colors) {
    if (rel->point_count < 2)
        return;

    nixie_strbuf_append(sb, "<polyline class=\"er-relationship\" points=\"");
    for (size_t i = 0; i < rel->point_count; i++) {
        if (i > 0)
            nixie_strbuf_append_char(sb, ' ');
        nixie_strbuf_appendf(sb, "%g,%g", rel->points[i].x, rel->points[i].y);
    }
    nixie_strbuf_appendf(sb, "\" fill=\"none\" stroke=\"%s\" stroke-width=\"1\"%s />", colors->line, rel->identifying ? "" : " stroke-dasharray=\"6 4\"");
}

/*
 * Crow's-foot marker at one endpoint. `point` is the endpoint, `toward`
 * gives the direction the line comes from -- ported directly from
 * beautiful-mermaid/src/er/renderer.ts's renderCrowsFoot().
 */
static void render_crows_foot(nixie_strbuf_t *sb, nixie_point_t point, nixie_point_t toward, nixie_er_cardinality_t card, const nixie_resolved_colors_t *colors) {
    double sw = 1.0 + 0.25;
    double dx = point.x - toward.x, dy = point.y - toward.y;
    double len = sqrt(dx * dx + dy * dy);
    if (len == 0.0)
        return;
    double ux = dx / len, uy = dy / len;
    double px = -uy, py = ux;

    double tip_x = point.x - ux * 4.0, tip_y = point.y - uy * 4.0;
    double back_x = point.x - ux * 16.0, back_y = point.y - uy * 16.0;

    int has_one_line   = card == NIXIE_ER_ONE || card == NIXIE_ER_ZERO_ONE;
    int has_crows_foot = card == NIXIE_ER_MANY || card == NIXIE_ER_ZERO_MANY;
    int has_circle     = card == NIXIE_ER_ZERO_ONE || card == NIXIE_ER_ZERO_MANY;

    if (has_one_line) {
        double half_w = 6.0;
        nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"%g\" />", tip_x + px * half_w, tip_y + py * half_w, tip_x - px * half_w, tip_y - py * half_w, colors->line, sw);
        double line2x = tip_x - ux * 4.0, line2y = tip_y - uy * 4.0;
        nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"%g\" />", line2x + px * half_w, line2y + py * half_w, line2x - px * half_w, line2y - py * half_w, colors->line, sw);
    }

    if (has_crows_foot) {
        double fan_w = 7.0;
        nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"%g\" />", tip_x + px * fan_w, tip_y + py * fan_w, back_x, back_y, colors->line, sw);
        nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"%g\" />", tip_x, tip_y, back_x, back_y, colors->line, sw);
        nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"%g\" />", tip_x - px * fan_w, tip_y - py * fan_w, back_x, back_y, colors->line, sw);
    }

    if (has_circle) {
        double circle_offset = has_crows_foot ? 20.0 : 12.0;
        double cx = point.x - ux * circle_offset, cy = point.y - uy * circle_offset;
        nixie_strbuf_appendf(sb, "<circle cx=\"%g\" cy=\"%g\" r=\"4\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />", cx, cy, colors->bg, colors->line, sw);
    }
}

static void render_cardinality(nixie_strbuf_t *sb, const nixie_pe_relationship_t *rel, const nixie_resolved_colors_t *colors) {
    if (rel->point_count < 2)
        return;
    render_crows_foot(sb, rel->points[0], rel->points[1], rel->cardinality1, colors);
    render_crows_foot(sb, rel->points[rel->point_count - 1], rel->points[rel->point_count - 2], rel->cardinality2, colors);
}

static double point_dist(nixie_point_t a, nixie_point_t b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    return sqrt(dx * dx + dy * dy);
}

/* Arc-length midpoint (not the naive geometric center of the first/last
 * point), so the label sits ON the path even for orthogonal routes with
 * bends -- ported from renderer.ts's midpoint(). */
static nixie_point_t arc_midpoint(const nixie_point_t *points, size_t count) {
    if (count == 0)
        return (nixie_point_t){ 0, 0 };
    if (count == 1)
        return points[0];

    double total = 0.0;
    for (size_t i = 1; i < count; i++)
        total += point_dist(points[i - 1], points[i]);
    if (total == 0.0)
        return points[0];

    double half = total / 2.0, walked = 0.0;
    for (size_t i = 1; i < count; i++) {
        double seg = point_dist(points[i - 1], points[i]);
        if (walked + seg >= half) {
            double t = seg > 0.0 ? (half - walked) / seg : 0.0;
            return (nixie_point_t){
                points[i - 1].x + (points[i].x - points[i - 1].x) * t,
                points[i - 1].y + (points[i].y - points[i - 1].y) * t,
            };
        }
        walked += seg;
    }
    return points[count - 1];
}

static void render_relationship_label(nixie_strbuf_t *sb, const nixie_pe_relationship_t *rel, const nixie_resolved_colors_t *colors) {
    if (rel->label == NULL || rel->label[0] == '\0' || rel->point_count < 2)
        return;

    nixie_point_t mid = arc_midpoint(rel->points, rel->point_count);
    double width      = nixie_measure_text_width(rel->label, 11.0, 400);
    size_t lines      = count_lines(rel->label);
    double height     = (double)lines * 11.0 * NIXIE_LINE_HEIGHT_RATIO;
    double bg_w = width + 8.0, bg_h = height + 6.0;

    nixie_strbuf_appendf(sb, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"2\" ry=\"2\" fill=\"%s\" stroke=\"%s\" stroke-width=\"0.5\" />\n", mid.x - bg_w / 2.0, mid.y - bg_h / 2.0, bg_w, bg_h, colors->bg, colors->inner_stroke);

    char attrs[160];
    snprintf(attrs, sizeof(attrs), "text-anchor=\"middle\" font-size=\"11\" font-weight=\"400\" fill=\"%s\"", colors->text_muted);
    render_multiline_text(sb, rel->label, mid.x, mid.y, 11.0, attrs);
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

char *nixie_er_render_svg(const nixie_positioned_er_diagram_t *pcd, const nixie_resolved_colors_t *colors, int transparent) {
    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    nixie_strbuf_appendf(&sb, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %g %g\" width=\"%g\" height=\"%g\"", pcd->width, pcd->height, pcd->width, pcd->height);
    if (!transparent) {
        nixie_strbuf_appendf(&sb, " style=\"background:%s\"", colors->bg);
    }
    nixie_strbuf_append(&sb, ">\n");
    nixie_strbuf_append(&sb, "<style>text{font-family:'Inter',system-ui,sans-serif;}.mono{font-family:'JetBrains Mono','SF Mono',monospace;}</style>\n");

    for (size_t i = 0; i < pcd->relationship_count; i++) {
        render_relationship_line(&sb, &pcd->relationships[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }

    for (size_t i = 0; i < pcd->node_count; i++) {
        render_entity_box(&sb, &pcd->nodes[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }

    for (size_t i = 0; i < pcd->relationship_count; i++) {
        render_cardinality(&sb, &pcd->relationships[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }

    for (size_t i = 0; i < pcd->relationship_count; i++) {
        render_relationship_label(&sb, &pcd->relationships[i], colors);
    }

    nixie_strbuf_append(&sb, "</svg>");
    return nixie_strbuf_release(&sb);
}
