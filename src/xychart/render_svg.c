#include "render_svg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../color.h"
#include "../strbuf.h"
#include "colors.h"

#define FONT_TITLE_SIZE 18.0
#define FONT_TITLE_WEIGHT 600
#define FONT_AXIS_TITLE_SIZE 15.0
#define FONT_AXIS_TITLE_WEIGHT 500
#define FONT_LABEL_SIZE 14.0
#define FONT_LABEL_WEIGHT 400
#define FONT_LEGEND_SIZE 14.0
#define FONT_LEGEND_WEIGHT 400
#define DOT_RADIUS 5.0
#define LINE_WIDTH 2.5
#define BAR_RADIUS 8.0

static void append_escaped_xml(nixie_strbuf_t *sb, const char *text) {
    if (text == NULL)
        return;
    for (const char *p = text; *p != '\0'; p++) {
        switch (*p) {
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
        default:
            nixie_strbuf_append_char(sb, *p);
            break;
        }
    }
}

/* Bar fill: a 25%-opacity-equivalent blend of the series color into the
 * background, ported from chartStyles()'s
 * "color-mix(in srgb, var(--bg) 75%, seriesColor 25%)". */
static void bar_fill_hex(const char *series_hex, const char *bg_hex, char out[8]) {
    nixie_rgb_t series_rgb, bg_rgb;
    if (nixie_parse_hex(series_hex, &series_rgb) != 0 || nixie_parse_hex(bg_hex, &bg_rgb) != 0) {
        nixie_format_hex((nixie_rgb_t){ 0, 0, 0 }, out);
        return;
    }
    nixie_rgb_t mixed = nixie_mix_rgb(series_rgb, bg_rgb, 25);
    nixie_format_hex(mixed, out);
}

/* ==========================================================================
 * Bar paths -- all four corners rounded on the "outward" side only
 * (top for vertical charts, right for horizontal ones).
 * ========================================================================== */

static void append_rounded_top_bar_path(nixie_strbuf_t *sb, double x, double y, double w, double h, double radius) {
    double rr = radius;
    if (w / 2.0 < rr)
        rr = w / 2.0;
    if (h / 2.0 < rr)
        rr = h / 2.0;
    if (rr <= 0.0) {
        nixie_strbuf_appendf(sb, "M%g,%g h%g v%g h%g Z", x, y, w, h, -w);
        return;
    }
    nixie_strbuf_appendf(sb, "M%g,%g Q%g,%g %g,%g L%g,%g Q%g,%g %g,%g L%g,%g Q%g,%g %g,%g L%g,%g Q%g,%g %g,%g Z", x, y + rr, x, y, x + rr, y, x + w - rr, y, x + w, y, x + w, y + rr, x + w, y + h - rr, x + w, y + h, x + w - rr, y + h, x + rr, y + h, x, y + h, x, y + h - rr);
}

static void append_rounded_right_bar_path(nixie_strbuf_t *sb, double x, double y, double w, double h, double radius) {
    double rr = radius;
    if (w / 2.0 < rr)
        rr = w / 2.0;
    if (h / 2.0 < rr)
        rr = h / 2.0;
    if (rr <= 0.0) {
        nixie_strbuf_appendf(sb, "M%g,%g h%g v%g h%g Z", x, y, w, h, -w);
        return;
    }
    nixie_strbuf_appendf(sb, "M%g,%g L%g,%g Q%g,%g %g,%g L%g,%g Q%g,%g %g,%g L%g,%g Q%g,%g %g,%g L%g,%g Q%g,%g %g,%g Z", x + rr, y, x + w - rr, y, x + w, y, x + w, y + rr, x + w, y + h - rr, x + w, y + h, x + w - rr, y + h, x + rr, y + h, x, y + h, x, y + h - rr, x, y + rr, x, y, x + rr, y);
}

/* ==========================================================================
 * Smooth line interpolation -- natural cubic spline, ported from
 * renderer.ts's smoothCurvePath() (Thomas algorithm for the tridiagonal
 * system of second derivatives, then one cubic Bezier segment per pair of
 * knots).
 * ========================================================================== */

static void append_smooth_curve_path(nixie_strbuf_t *sb, const nixie_xy_line_point_t *points, size_t n) {
    if (n == 0)
        return;
    if (n == 1) {
        nixie_strbuf_appendf(sb, "M%g,%g", points[0].x, points[0].y);
        return;
    }
    if (n == 2) {
        nixie_strbuf_appendf(sb, "M%g,%g L%g,%g", points[0].x, points[0].y, points[1].x, points[1].y);
        return;
    }

    double *h     = (double *)malloc((n - 1) * sizeof(double));
    double *delta = (double *)malloc((n - 1) * sizeof(double));
    for (size_t i = 0; i < n - 1; i++) {
        h[i]     = points[i + 1].x - points[i].x;
        delta[i] = h[i] == 0.0 ? 0.0 : (points[i + 1].y - points[i].y) / h[i];
    }

    double *c = (double *)calloc(n, sizeof(double));
    if (n > 2) {
        double *cp = (double *)calloc(n, sizeof(double));
        double *dp = (double *)calloc(n, sizeof(double));
        for (size_t i = 1; i < n - 1; i++) {
            double diag = 2.0 * (h[i - 1] + h[i]);
            double rhs  = 3.0 * (delta[i] - delta[i - 1]);
            if (i == 1) {
                cp[i] = h[i] / diag;
                dp[i] = rhs / diag;
            } else {
                double wdenom = diag - h[i - 1] * cp[i - 1];
                cp[i]         = h[i] / wdenom;
                dp[i]         = (rhs - h[i - 1] * dp[i - 1]) / wdenom;
            }
        }
        for (size_t k = 0; k < n - 2; k++) {
            size_t i = n - 2 - k; /* n-2 downto 1, as a forward-counting loop to avoid unsigned underflow at 0 */
            c[i]     = dp[i] - cp[i] * c[i + 1];
        }
        free(cp);
        free(dp);
    }

    double *slopes = (double *)calloc(n, sizeof(double));
    for (size_t i = 0; i < n - 1; i++) {
        slopes[i] = delta[i] - h[i] * (2.0 * c[i] + c[i + 1]) / 3.0;
    }
    slopes[n - 1] = delta[n - 2] + h[n - 2] * c[n - 2] / 3.0;

    nixie_strbuf_appendf(sb, "M%g,%g", points[0].x, points[0].y);
    for (size_t i = 0; i < n - 1; i++) {
        double seg  = h[i] / 3.0;
        double cp1x = points[i].x + seg;
        double cp1y = points[i].y + slopes[i] * seg;
        double cp2x = points[i + 1].x - seg;
        double cp2y = points[i + 1].y - slopes[i + 1] * seg;
        nixie_strbuf_appendf(sb, " C%g,%g %g,%g %g,%g", cp1x, cp1y, cp2x, cp2y, points[i + 1].x, points[i + 1].y);
    }

    free(h);
    free(delta);
    free(c);
    free(slopes);
}

/* ==========================================================================
 * Axis / title text
 * ========================================================================== */

static const char *anchor_str(nixie_xy_text_anchor_t a) {
    switch (a) {
    case NIXIE_XY_ANCHOR_START:
        return "start";
    case NIXIE_XY_ANCHOR_END:
        return "end";
    default:
        return "middle";
    }
}

static void append_ticks(nixie_strbuf_t *sb, const nixie_xy_tick_t *ticks, size_t count, const char *fill) {
    for (size_t i = 0; i < count; i++) {
        nixie_strbuf_appendf(sb, "<text x=\"%g\" y=\"%g\" text-anchor=\"%s\" font-size=\"%g\" font-weight=\"%d\" dy=\"0.35em\" fill=\"%s\">", ticks[i].label_x, ticks[i].label_y, anchor_str(ticks[i].text_anchor), FONT_LABEL_SIZE, FONT_LABEL_WEIGHT, fill);
        append_escaped_xml(sb, ticks[i].label);
        nixie_strbuf_append(sb, "</text>\n");
    }
}

static void append_axis_title(nixie_strbuf_t *sb, const nixie_positioned_axis_t *axis, const char *fill) {
    if (!axis->has_title)
        return;
    char transform[96];
    transform[0] = '\0';
    if (axis->title_rotate != 0.0) {
        snprintf(transform, sizeof(transform), " transform=\"rotate(%g,%g,%g)\"", axis->title_rotate, axis->title_x, axis->title_y);
    }
    nixie_strbuf_appendf(sb, "<text x=\"%g\" y=\"%g\" text-anchor=\"middle\"%s font-size=\"%g\" font-weight=\"%d\" dy=\"0.35em\" fill=\"%s\">", axis->title_x, axis->title_y, transform, FONT_AXIS_TITLE_SIZE, FONT_AXIS_TITLE_WEIGHT, fill);
    append_escaped_xml(sb, axis->title_text);
    nixie_strbuf_append(sb, "</text>\n");
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

char *nixie_xy_render_svg(const nixie_positioned_xy_chart_t *pc, const nixie_resolved_colors_t *colors, const char *raw_accent_hex, int transparent) {
    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    nixie_strbuf_appendf(&sb, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %g %g\" width=\"%g\" height=\"%g\"", pc->width, pc->height, pc->width, pc->height);
    if (!transparent) {
        nixie_strbuf_appendf(&sb, " style=\"background:%s\"", colors->bg);
    }
    nixie_strbuf_append(&sb, ">\n");
    nixie_strbuf_append(&sb, "<style>text{font-family:'Inter',system-ui,sans-serif;}</style>\n");

    int max_idx = -1;
    for (size_t i = 0; i < pc->bar_count; i++) {
        if (pc->bars[i].color_index > max_idx)
            max_idx = pc->bars[i].color_index;
    }
    for (size_t i = 0; i < pc->line_count; i++) {
        if (pc->lines[i].color_index > max_idx)
            max_idx = pc->lines[i].color_index;
    }
    size_t n_colors       = max_idx >= 0 ? (size_t)(max_idx + 1) : 1;
    char (*series_hex)[8] = (char (*)[8])malloc(n_colors * sizeof(*series_hex));
    for (size_t i = 0; i < n_colors; i++) {
        nixie_xy_series_color((int)i, raw_accent_hex, colors->bg, series_hex[i]);
    }

    /* Grid lines: a simplified stand-in for beautiful-mermaid's dense
     * dot-grid pattern (purely decorative there) -- subtle lines at each
     * tick serve the same "reading aid" purpose with far less code. */
    for (size_t i = 0; i < pc->grid_line_count; i++) {
        nixie_strbuf_appendf(&sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"1\" opacity=\"0.5\" />\n", pc->grid_lines[i].x1, pc->grid_lines[i].y1, pc->grid_lines[i].x2, pc->grid_lines[i].y2, colors->inner_stroke);
    }

    /* Bars */
    for (size_t i = 0; i < pc->bar_count; i++) {
        const nixie_positioned_bar_t *bar = &pc->bars[i];
        const char *series_color          = series_hex[bar->color_index];
        char fill[8];
        bar_fill_hex(series_color, colors->bg, fill);

        nixie_strbuf_append(&sb, "<path d=\"");
        if (pc->horizontal) {
            append_rounded_right_bar_path(&sb, bar->x, bar->y, bar->w, bar->h, BAR_RADIUS);
        } else {
            append_rounded_top_bar_path(&sb, bar->x, bar->y, bar->w, bar->h, BAR_RADIUS);
        }
        nixie_strbuf_appendf(&sb, "\" fill=\"%s\" stroke=\"%s\" stroke-width=\"1.5\" data-value=\"%g\"", fill, series_color, bar->value);
        if (bar->label != NULL) {
            nixie_strbuf_append(&sb, " data-label=\"");
            append_escaped_xml(&sb, bar->label);
            nixie_strbuf_append(&sb, "\"");
        }
        nixie_strbuf_append(&sb, " />\n");
    }

    /* Lines: low-opacity shadow (slightly offset) then a crisp line on top. */
    size_t max_line_points = 0;
    for (size_t i = 0; i < pc->line_count; i++) {
        if (pc->lines[i].point_count > max_line_points)
            max_line_points = pc->lines[i].point_count;
    }
    int sparse = max_line_points > 0 && max_line_points <= 12;

    for (size_t i = 0; i < pc->line_count; i++) {
        const nixie_positioned_line_t *line = &pc->lines[i];
        if (line->point_count == 0)
            continue;
        const char *series_color = series_hex[line->color_index];

        nixie_strbuf_append(&sb, "<path d=\"");
        append_smooth_curve_path(&sb, line->points, line->point_count);
        nixie_strbuf_appendf(&sb,
            "\" fill=\"none\" stroke=\"%s\" stroke-width=\"5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" "
            "opacity=\"0.12\" transform=\"translate(0,2)\" />\n",
            series_color);

        nixie_strbuf_append(&sb, "<path d=\"");
        append_smooth_curve_path(&sb, line->points, line->point_count);
        nixie_strbuf_appendf(&sb, "\" fill=\"none\" stroke=\"%s\" stroke-width=\"%g\" stroke-linecap=\"round\" stroke-linejoin=\"round\" />\n", series_color, LINE_WIDTH);

        if (sparse) {
            for (size_t k = 0; k < line->point_count; k++) {
                nixie_strbuf_appendf(&sb, "<circle cx=\"%g\" cy=\"%g\" r=\"%g\" fill=\"%s\" stroke=\"%s\" stroke-width=\"2\" />\n", line->points[k].x, line->points[k].y, DOT_RADIUS, series_color, colors->bg);
            }
        }
    }

    /* Axis labels */
    append_ticks(&sb, pc->x_axis.ticks, pc->x_axis.tick_count, colors->text_muted);
    append_ticks(&sb, pc->y_axis.ticks, pc->y_axis.tick_count, colors->text_muted);

    /* Axis titles */
    append_axis_title(&sb, &pc->x_axis, colors->text_sec);
    append_axis_title(&sb, &pc->y_axis, colors->text_sec);

    /* Chart title */
    if (pc->has_title) {
        nixie_strbuf_appendf(&sb, "<text x=\"%g\" y=\"%g\" text-anchor=\"middle\" font-size=\"%g\" font-weight=\"%d\" dy=\"0.35em\" fill=\"%s\">", pc->title_x, pc->title_y, FONT_TITLE_SIZE, FONT_TITLE_WEIGHT, colors->text);
        append_escaped_xml(&sb, pc->title_text);
        nixie_strbuf_append(&sb, "</text>\n");
    }

    /* Legend */
    for (size_t i = 0; i < pc->legend_count; i++) {
        const nixie_xy_legend_item_t *item = &pc->legend[i];
        const char *series_color           = series_hex[item->color_index];
        double swatch_w = 14.0, swatch_h = 14.0, gap = 6.0;

        if (item->type == NIXIE_XY_BAR) {
            nixie_strbuf_appendf(&sb, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"3\" fill=\"%s\" />\n", item->x, item->y - swatch_h / 2.0, swatch_w, swatch_h, series_color);
        } else {
            nixie_strbuf_appendf(&sb, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"%g\" stroke-linecap=\"round\" />\n", item->x, item->y, item->x + swatch_w, item->y, series_color, LINE_WIDTH);
        }
        nixie_strbuf_appendf(&sb, "<text x=\"%g\" y=\"%g\" text-anchor=\"start\" font-size=\"%g\" font-weight=\"%d\" dy=\"0.35em\" fill=\"%s\">", item->x + swatch_w + gap, item->y, FONT_LEGEND_SIZE, FONT_LEGEND_WEIGHT, colors->text_muted);
        append_escaped_xml(&sb, item->label);
        nixie_strbuf_append(&sb, "</text>\n");
    }

    free(series_hex);

    nixie_strbuf_append(&sb, "</svg>");
    return nixie_strbuf_release(&sb);
}
