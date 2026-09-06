#include "render_svg.h"

#include <stdio.h>
#include <string.h>

#include "../strbuf.h"
#include "../text_metrics.h"

#define FONT_SIZE_NODE_LABEL 13.0
#define FONT_WEIGHT_NODE_LABEL 500
#define FONT_SIZE_EDGE_LABEL 11.0
#define FONT_WEIGHT_EDGE_LABEL 400
#define FONT_WEIGHT_GROUP_HEADER 600
#define STROKE_WIDTH_OUTER 1.0
#define STROKE_WIDTH_INNER 0.75
#define STROKE_WIDTH_CONNECTOR 1.0
#define ARROW_HEAD_W 8.0
#define ARROW_HEAD_H 5.0

/* ==========================================================================
 * XML escaping / multi-line text (small local copy -- see
 * flowchart/render_svg.c's note on why these aren't factored into a shared
 * module).
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
 * Arrow markers
 * ========================================================================== */

static void append_marker_defs(nixie_strbuf_t *sb, const nixie_resolved_colors_t *colors) {
    double w = ARROW_HEAD_W, h = ARROW_HEAD_H;
    nixie_strbuf_appendf(sb,
        "<marker id=\"seq-arrow\" markerWidth=\"%g\" markerHeight=\"%g\" refX=\"%g\" refY=\"%g\" orient=\"auto-start-reverse\">"
        "<polygon points=\"0 0, %g %g, 0 %g\" fill=\"%s\" /></marker>\n",
        w, h, w, h / 2.0, w, h / 2.0, h, colors->arrow);
    nixie_strbuf_appendf(sb,
        "<marker id=\"seq-arrow-open\" markerWidth=\"%g\" markerHeight=\"%g\" refX=\"%g\" refY=\"%g\" orient=\"auto-start-reverse\">"
        "<polyline points=\"0 0, %g %g, 0 %g\" fill=\"none\" stroke=\"%s\" stroke-width=\"1\" /></marker>\n",
        w, h, w, h / 2.0, w, h / 2.0, h, colors->arrow);
}

/* ==========================================================================
 * Actors
 * ========================================================================== */

static void render_actor(nixie_strbuf_t *sb, const nixie_ps_actor_t *actor, const nixie_resolved_colors_t *colors) {
    double x = actor->x, y = actor->y, w = actor->width, h = actor->height;

    nixie_strbuf_append(sb, "<g class=\"actor\" data-id=\"");
    append_escaped_attr(sb, actor->id);
    nixie_strbuf_append(sb, "\" data-label=\"");
    append_escaped_attr(sb, actor->label);
    nixie_strbuf_appendf(sb, "\" data-type=\"%s\">\n", actor->type == NIXIE_SEQ_ACTOR ? "actor" : "participant");

    if (actor->type == NIXIE_SEQ_ACTOR) {
        /* Stick-figure icon in a 24x24 space, scaled to 90% of the actor
         * box height and centered in it -- ported verbatim from
         * renderer.ts's renderActor(). */
        double s  = (h / 24.0) * 0.9;
        double tx = x - 12.0 * s;
        double ty = y + (h - 24.0 * s) / 2.0;
        double sw = STROKE_WIDTH_OUTER / s;

        nixie_strbuf_appendf(sb, "<g transform=\"translate(%g,%g) scale(%g)\">\n", tx, ty, s);
        nixie_strbuf_appendf(sb,
            "<path d=\"M21 12C21 16.9706 16.9706 21 12 21C7.02944 21 3 16.9706 3 12C3 7.02944 7.02944 3 12 3C16.9706 3 21 7.02944 21 12Z\" "
            "fill=\"none\" stroke=\"%s\" stroke-width=\"%g\" />\n",
            colors->line, sw);
        nixie_strbuf_appendf(sb,
            "<path d=\"M15 10C15 11.6569 13.6569 13 12 13C10.3431 13 9 11.6569 9 10C9 8.34315 10.3431 7 12 7C13.6569 7 15 8.34315 15 10Z\" "
            "fill=\"none\" stroke=\"%s\" stroke-width=\"%g\" />\n",
            colors->line, sw);
        nixie_strbuf_appendf(sb,
            "<path d=\"M5.62842 18.3563C7.08963 17.0398 9.39997 16 12 16C14.6 16 16.9104 17.0398 18.3716 18.3563\" "
            "fill=\"none\" stroke=\"%s\" stroke-width=\"%g\" />\n",
            colors->line, sw);
        nixie_strbuf_append(sb, "</g>\n");

        char label_attrs[160];
        snprintf(label_attrs, sizeof(label_attrs), "font-size=\"%g\" text-anchor=\"middle\" font-weight=\"%d\" fill=\"%s\"", FONT_SIZE_NODE_LABEL, FONT_WEIGHT_NODE_LABEL, colors->text);
        render_multiline_text(sb, actor->label, x, y + h + 14.0, FONT_SIZE_NODE_LABEL, label_attrs);
        nixie_strbuf_append(sb, "\n");
    } else {
        double box_x = x - w / 2.0;
        nixie_strbuf_appendf(sb, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"4\" ry=\"4\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />\n", box_x, y, w, h, colors->node_fill, colors->node_stroke, STROKE_WIDTH_OUTER);

        char label_attrs[160];
        snprintf(label_attrs, sizeof(label_attrs), "font-size=\"%g\" text-anchor=\"middle\" font-weight=\"%d\" fill=\"%s\"", FONT_SIZE_NODE_LABEL, FONT_WEIGHT_NODE_LABEL, colors->text);
        render_multiline_text(sb, actor->label, x, y + h / 2.0, FONT_SIZE_NODE_LABEL, label_attrs);
        nixie_strbuf_append(sb, "\n");
    }

    nixie_strbuf_append(sb, "</g>");
}

/* ==========================================================================
 * Lifelines / activations
 * ========================================================================== */

static void render_lifeline(nixie_strbuf_t *sb, const nixie_ps_lifeline_t *ll, const nixie_resolved_colors_t *colors) {
    nixie_strbuf_append(sb, "<line class=\"lifeline\" data-actor=\"");
    append_escaped_attr(sb, ll->actor_id);
    nixie_strbuf_appendf(sb, "\" x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"0.75\" stroke-dasharray=\"6 4\" />", ll->x, ll->top_y, ll->x, ll->bottom_y, colors->line);
}

static void render_activation(nixie_strbuf_t *sb, const nixie_ps_activation_t *act, const nixie_resolved_colors_t *colors) {
    nixie_strbuf_append(sb, "<rect class=\"activation\" data-actor=\"");
    append_escaped_attr(sb, act->actor_id);
    nixie_strbuf_appendf(sb, "\" x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />", act->x, act->top_y, act->width, act->bottom_y - act->top_y, colors->node_fill, colors->node_stroke, STROKE_WIDTH_INNER);
}

/* ==========================================================================
 * Messages
 * ========================================================================== */

static void render_message(nixie_strbuf_t *sb, const nixie_ps_message_t *msg, const nixie_resolved_colors_t *colors) {
    const char *dash_array = msg->line_style == NIXIE_SEQ_DASHED ? " stroke-dasharray=\"6 4\"" : "";
    const char *marker_id  = msg->arrow_head == NIXIE_SEQ_FILLED ? "seq-arrow" : "seq-arrow-open";

    nixie_strbuf_append(sb, "<g class=\"message\" data-from=\"");
    append_escaped_attr(sb, msg->from);
    nixie_strbuf_append(sb, "\" data-to=\"");
    append_escaped_attr(sb, msg->to);
    nixie_strbuf_append(sb, "\" data-label=\"");
    append_escaped_attr(sb, msg->label);
    nixie_strbuf_appendf(sb, "\" data-line-style=\"%s\" data-arrow-head=\"%s\" data-self=\"%s\">\n", msg->line_style == NIXIE_SEQ_DASHED ? "dashed" : "solid", msg->arrow_head == NIXIE_SEQ_FILLED ? "filled" : "open", msg->is_self ? "true" : "false");

    char attrs[160];

    if (msg->is_self) {
        double loop_w = 30.0, loop_h = 20.0, label_padding = 8.0;
        nixie_strbuf_appendf(sb, "<polyline points=\"%g,%g %g,%g %g,%g %g,%g\" fill=\"none\" stroke=\"%s\" stroke-width=\"%g\"%s marker-end=\"url(#%s)\" />\n", msg->x1, msg->y, msg->x1 + loop_w, msg->y, msg->x1 + loop_w, msg->y + loop_h, msg->x2, msg->y + loop_h, colors->line, STROKE_WIDTH_CONNECTOR, dash_array, marker_id);

        snprintf(attrs, sizeof(attrs), "font-size=\"%g\" text-anchor=\"start\" font-weight=\"%d\" fill=\"%s\"", FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_EDGE_LABEL, colors->text_muted);
        render_multiline_text(sb, msg->label, msg->x1 + loop_w + label_padding, msg->y + loop_h / 2.0, FONT_SIZE_EDGE_LABEL, attrs);
        nixie_strbuf_append(sb, "\n");
    } else {
        nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"%g\"%s marker-end=\"url(#%s)\" />\n", msg->x1, msg->y, msg->x2, msg->y, colors->line, STROKE_WIDTH_CONNECTOR, dash_array, marker_id);

        double mid_x = (msg->x1 + msg->x2) / 2.0;
        snprintf(attrs, sizeof(attrs), "font-size=\"%g\" text-anchor=\"middle\" font-weight=\"%d\" fill=\"%s\"", FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_EDGE_LABEL, colors->text_muted);
        render_multiline_text(sb, msg->label, mid_x, msg->y - 10.0, FONT_SIZE_EDGE_LABEL, attrs);
        nixie_strbuf_append(sb, "\n");
    }

    nixie_strbuf_append(sb, "</g>");
}

/* ==========================================================================
 * Blocks
 * ========================================================================== */

static const char *block_type_name(nixie_seq_block_type_t type) {
    switch (type) {
    case NIXIE_SEQ_BLOCK_LOOP:
        return "loop";
    case NIXIE_SEQ_BLOCK_ALT:
        return "alt";
    case NIXIE_SEQ_BLOCK_OPT:
        return "opt";
    case NIXIE_SEQ_BLOCK_PAR:
        return "par";
    case NIXIE_SEQ_BLOCK_CRITICAL:
        return "critical";
    case NIXIE_SEQ_BLOCK_BREAK:
        return "break";
    case NIXIE_SEQ_BLOCK_RECT:
    default:
        return "rect";
    }
}

static void render_block(nixie_strbuf_t *sb, const nixie_ps_block_t *block, const nixie_resolved_colors_t *colors) {
    const char *type_name = block_type_name(block->type);

    nixie_strbuf_appendf(sb, "<g class=\"block\" data-type=\"%s\"", type_name);
    if (block->label != NULL && block->label[0] != '\0') {
        nixie_strbuf_append(sb, " data-label=\"");
        append_escaped_attr(sb, block->label);
        nixie_strbuf_append_char(sb, '"');
    }
    nixie_strbuf_append(sb, ">\n");

    nixie_strbuf_appendf(sb, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"none\" stroke=\"%s\" stroke-width=\"%g\" />\n", block->x, block->y, block->width, block->height, colors->node_stroke, STROKE_WIDTH_OUTER);

    nixie_strbuf_t label_sb;
    nixie_strbuf_init(&label_sb);
    nixie_strbuf_append(&label_sb, type_name);
    if (block->label != NULL && block->label[0] != '\0') {
        nixie_strbuf_append(&label_sb, " [");
        nixie_strbuf_append(&label_sb, block->label);
        nixie_strbuf_append_char(&label_sb, ']');
    }
    char *label_text = label_sb.data != NULL ? label_sb.data : "";

    size_t first_line_len = 0;
    while (label_text[first_line_len] != '\0' && label_text[first_line_len] != '\n')
        first_line_len++;

    nixie_strbuf_t first_line_sb;
    nixie_strbuf_init(&first_line_sb);
    nixie_strbuf_append_n(&first_line_sb, label_text, first_line_len);
    double tab_w = nixie_measure_text_width(first_line_sb.data != NULL ? first_line_sb.data : "", FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_GROUP_HEADER) + 16.0;
    nixie_strbuf_free(&first_line_sb);
    double tab_h = 18.0;

    nixie_strbuf_appendf(sb, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />\n", block->x, block->y, tab_w, tab_h, colors->group_hdr, colors->node_stroke, STROKE_WIDTH_OUTER);

    char label_attrs[160];
    snprintf(label_attrs, sizeof(label_attrs), "font-size=\"%g\" font-weight=\"%d\" fill=\"%s\"", FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_GROUP_HEADER, colors->text_sec);
    render_multiline_text(sb, label_text, block->x + 6.0, block->y + tab_h / 2.0, FONT_SIZE_EDGE_LABEL, label_attrs);
    nixie_strbuf_append(sb, "\n");
    nixie_strbuf_free(&label_sb);

    for (size_t i = 0; i < block->divider_count; i++) {
        const nixie_ps_divider_t *div = &block->dividers[i];
        nixie_strbuf_appendf(sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"0.75\" stroke-dasharray=\"6 4\" />\n", block->x, div->y, block->x + block->width, div->y, colors->line);
        if (div->label != NULL && div->label[0] != '\0') {
            nixie_strbuf_t div_sb;
            nixie_strbuf_init(&div_sb);
            nixie_strbuf_append_char(&div_sb, '[');
            nixie_strbuf_append(&div_sb, div->label);
            nixie_strbuf_append_char(&div_sb, ']');

            char div_attrs[160];
            snprintf(div_attrs, sizeof(div_attrs), "font-size=\"%g\" text-anchor=\"start\" font-weight=\"%d\" fill=\"%s\"", FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_EDGE_LABEL, colors->text_muted);
            render_multiline_text(sb, div_sb.data != NULL ? div_sb.data : "", block->x + 8.0, div->y + 14.0, FONT_SIZE_EDGE_LABEL, div_attrs);
            nixie_strbuf_append(sb, "\n");
            nixie_strbuf_free(&div_sb);
        }
    }

    nixie_strbuf_append(sb, "</g>");
}

/* ==========================================================================
 * Notes
 * ========================================================================== */

static void render_note(nixie_strbuf_t *sb, const nixie_ps_note_t *note, const nixie_resolved_colors_t *colors) {
    double fold = 6.0;
    double x = note->x, y = note->y, w = note->width, h = note->height;

    nixie_strbuf_append(sb, "<g class=\"note\"");
    if (note->actor_count > 0) {
        nixie_strbuf_append(sb, " data-actors=\"");
        for (size_t i = 0; i < note->actor_count; i++) {
            if (i > 0)
                nixie_strbuf_append_char(sb, ',');
            append_escaped_attr(sb, note->actors[i]);
        }
        nixie_strbuf_append_char(sb, '"');
    }
    nixie_strbuf_append(sb, ">\n");

    nixie_strbuf_appendf(sb, "<polygon points=\"%g,%g %g,%g %g,%g %g,%g %g,%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />\n", x, y, x + w - fold, y, x + w, y + fold, x + w, y + h, x, y + h, colors->bg, colors->node_stroke, STROKE_WIDTH_INNER);
    nixie_strbuf_appendf(sb, "<polygon points=\"%g,%g %g,%g %g,%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" />\n", x + w - fold, y, x + w, y + fold, x + w - fold, y + fold, colors->inner_stroke, colors->node_stroke, STROKE_WIDTH_INNER);

    char attrs[160];
    snprintf(attrs, sizeof(attrs), "font-size=\"%g\" text-anchor=\"middle\" font-weight=\"%d\" fill=\"%s\"", FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_EDGE_LABEL, colors->text_muted);
    render_multiline_text(sb, note->text, x + w / 2.0, y + h / 2.0, FONT_SIZE_EDGE_LABEL, attrs);
    nixie_strbuf_append(sb, "\n");

    nixie_strbuf_append(sb, "</g>");
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

char *nixie_sequence_render_svg(const nixie_positioned_sequence_diagram_t *psd, const nixie_resolved_colors_t *colors, int transparent) {
    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    nixie_strbuf_appendf(&sb, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %g %g\" width=\"%g\" height=\"%g\"", psd->width, psd->height, psd->width, psd->height);
    if (!transparent) {
        nixie_strbuf_appendf(&sb, " style=\"background:%s\"", colors->bg);
    }
    nixie_strbuf_append(&sb, ">\n");
    nixie_strbuf_append(&sb, "<style>text{font-family:'Inter',system-ui,sans-serif;}</style>\n");

    nixie_strbuf_append(&sb, "<defs>\n");
    append_marker_defs(&sb, colors);
    nixie_strbuf_append(&sb, "</defs>\n");

    for (size_t i = 0; i < psd->block_count; i++) {
        render_block(&sb, &psd->blocks[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }
    for (size_t i = 0; i < psd->lifeline_count; i++) {
        render_lifeline(&sb, &psd->lifelines[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }
    for (size_t i = 0; i < psd->activation_count; i++) {
        render_activation(&sb, &psd->activations[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }
    for (size_t i = 0; i < psd->message_count; i++) {
        render_message(&sb, &psd->messages[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }
    for (size_t i = 0; i < psd->note_count; i++) {
        render_note(&sb, &psd->notes[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }
    for (size_t i = 0; i < psd->actor_count; i++) {
        render_actor(&sb, &psd->actors[i], colors);
        nixie_strbuf_append(&sb, "\n");
    }

    nixie_strbuf_append(&sb, "</svg>");

    return nixie_strbuf_release(&sb);
}
