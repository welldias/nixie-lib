#include "layout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../text_metrics.h"

/* Layout constants ported verbatim from beautiful-mermaid/src/xychart/layout.ts's XY object. */
#define XY_PLOT_WIDTH 600.0
#define XY_PLOT_HEIGHT 340.0
#define XY_PADDING 22.0
#define XY_TITLE_FONT_SIZE 18.0
#define XY_TITLE_HEIGHT 42.0
#define XY_AXIS_LABEL_FONT_SIZE 14.0
#define XY_AXIS_LABEL_FONT_WEIGHT 400
#define XY_X_LABEL_HEIGHT 38.0
#define XY_Y_LABEL_WIDTH 58.0
#define XY_Y_LABEL_GAP 18.0
#define XY_AXIS_TITLE_PAD 30.0
#define XY_TICK_LENGTH 4.0
#define XY_BAR_PAD_RATIO 0.2
#define XY_BAR_GROUP_GAP 0.0
#define XY_MAX_BAR_WIDTH 40.0
#define XY_LEGEND_FONT_SIZE 14.0
#define XY_LEGEND_FONT_WEIGHT 400
#define XY_LEGEND_HEIGHT 28.0
#define XY_LEGEND_SWATCH_W 14.0
#define XY_LEGEND_GAP 6.0
#define XY_LEGEND_ITEM_GAP 16.0

/* ==========================================================================
 * Shared helpers
 * ========================================================================== */

size_t nixie_xy_get_data_count(const nixie_xy_chart_t *chart) {
    if (chart->x_axis.category_count > 0) return chart->x_axis.category_count;
    for (size_t i = 0; i < chart->series_count; i++) {
        if (chart->series[i].data_count > 0) return chart->series[i].data_count;
    }
    return 1;
}

void nixie_xy_format_tick_value(double v, char *buf, size_t buflen) {
    double rounded = v >= 0.0 ? floor(v + 0.5) : ceil(v - 0.5);
    if (fabs(v - rounded) < 1e-9) {
        snprintf(buf, buflen, "%.0f", rounded);
    } else {
        int decimals = fabs(v) < 10.0 ? 1 : 0;
        snprintf(buf, buflen, "%.*f", decimals, v);
    }
}

char **nixie_xy_get_category_labels(nixie_arena_t *arena, const nixie_xy_chart_t *chart, size_t count) {
    if (chart->x_axis.category_count > 0) {
        return chart->x_axis.categories;
    }

    char **labels = (char **)nixie_arena_alloc(arena, count * sizeof(char *));

    if (chart->x_axis.has_range) {
        double min = chart->x_axis.range_min, max = chart->x_axis.range_max;
        double step = count > 1 ? (max - min) / (double)(count - 1) : 0.0;
        for (size_t i = 0; i < count; i++) {
            char buf[64];
            nixie_xy_format_tick_value(min + step * (double)i, buf, sizeof(buf));
            labels[i] = nixie_arena_strdup(arena, buf);
        }
    } else {
        for (size_t i = 0; i < count; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", i + 1);
            labels[i] = nixie_arena_strdup(arena, buf);
        }
    }

    return labels;
}

double *nixie_xy_nice_tick_values(nixie_arena_t *arena, double min, double max, size_t *out_count) {
    double range = max - min;
    if (range <= 0.0) {
        double *arr = (double *)nixie_arena_alloc(arena, sizeof(double));
        arr[0] = min;
        *out_count = 1;
        return arr;
    }

    double raw_interval = range / 6.0;
    double magnitude = pow(10.0, floor(log10(raw_interval)));
    double residual = raw_interval / magnitude;
    double nice_interval;
    if (residual <= 1.5) nice_interval = magnitude;
    else if (residual <= 3.0) nice_interval = 2.0 * magnitude;
    else if (residual <= 7.0) nice_interval = 5.0 * magnitude;
    else nice_interval = 10.0 * magnitude;

    double start = ceil(min / nice_interval) * nice_interval;

    size_t cap = 16, count = 0;
    double *arr = (double *)nixie_arena_alloc(arena, cap * sizeof(double));
    for (double v = start; v <= max + nice_interval * 0.001; v += nice_interval) {
        double rv = round(v * 1e10) / 1e10;
        if (count == cap) {
            size_t new_cap = cap * 2;
            double *new_arr = (double *)nixie_arena_alloc(arena, new_cap * sizeof(double));
            memcpy(new_arr, arr, count * sizeof(double));
            arr = new_arr;
            cap = new_cap;
        }
        arr[count++] = rv;
    }

    *out_count = count;
    return arr;
}

static double estimate_label_width(const char *text) {
    return nixie_measure_text_width(text, XY_AXIS_LABEL_FONT_SIZE, XY_AXIS_LABEL_FONT_WEIGHT);
}

static size_t count_series_of_type(const nixie_xy_chart_t *chart, nixie_xy_series_type_t type) {
    size_t n = 0;
    for (size_t i = 0; i < chart->series_count; i++) {
        if (chart->series[i].type == type) n++;
    }
    return n;
}

static size_t count_total_bars(const nixie_xy_chart_t *chart) {
    size_t n = 0;
    for (size_t i = 0; i < chart->series_count; i++) {
        if (chart->series[i].type == NIXIE_XY_BAR) n += chart->series[i].data_count;
    }
    return n;
}

/* ==========================================================================
 * Legend
 * ========================================================================== */

static nixie_xy_legend_item_t *build_legend_items(
    nixie_arena_t *arena, const nixie_xy_chart_t *chart, double center_x, double y, size_t *out_count) {
    size_t n = chart->series_count;
    nixie_xy_legend_item_t *items = (nixie_xy_legend_item_t *)nixie_arena_alloc(arena, n * sizeof(nixie_xy_legend_item_t));
    double *widths = (double *)nixie_arena_alloc(arena, n * sizeof(double));

    int bar_idx = 0, line_idx = 0;
    double total_width = 0.0;
    for (size_t si = 0; si < n; si++) {
        const nixie_xy_series_t *s = &chart->series[si];
        char label[32];
        if (s->type == NIXIE_XY_BAR) {
            snprintf(label, sizeof(label), "Bar %d", bar_idx + 1);
        } else {
            snprintf(label, sizeof(label), "Line %d", line_idx + 1);
        }

        items[si].label = nixie_arena_strdup(arena, label);
        items[si].y = y;
        items[si].type = s->type;
        items[si].series_index = s->type == NIXIE_XY_BAR ? bar_idx : line_idx;
        items[si].color_index = (int)si;

        double text_w = nixie_measure_text_width(label, XY_LEGEND_FONT_SIZE, XY_LEGEND_FONT_WEIGHT);
        widths[si] = XY_LEGEND_SWATCH_W + XY_LEGEND_GAP + text_w;
        total_width += widths[si];

        if (s->type == NIXIE_XY_BAR) bar_idx++;
        else line_idx++;
    }
    if (n > 0) total_width += (double)(n - 1) * XY_LEGEND_ITEM_GAP;

    double x = center_x - total_width / 2.0;
    for (size_t si = 0; si < n; si++) {
        items[si].x = x;
        x += widths[si] + XY_LEGEND_ITEM_GAP;
    }

    *out_count = n;
    return items;
}

/* ==========================================================================
 * Vertical layout (default)
 * ========================================================================== */

static nixie_positioned_xy_chart_t *layout_vertical(nixie_arena_t *arena, const nixie_xy_chart_t *chart) {
    int has_title = chart->title != NULL;
    int has_x_title = chart->x_axis.title != NULL;
    int has_y_title = chart->y_axis.title != NULL;
    int has_legend = chart->series_count > 1;

    double y_min = chart->y_axis.range_min, y_max = chart->y_axis.range_max;
    size_t y_tick_count;
    double *y_ticks = nixie_xy_nice_tick_values(arena, y_min, y_max, &y_tick_count);

    double max_y_label_w = XY_Y_LABEL_WIDTH;
    for (size_t i = 0; i < y_tick_count; i++) {
        char buf[64];
        nixie_xy_format_tick_value(y_ticks[i], buf, sizeof(buf));
        double w = estimate_label_width(buf);
        if (w > max_y_label_w) max_y_label_w = w;
    }

    double top = XY_PADDING + (has_title ? XY_TITLE_HEIGHT : 0.0) + (has_legend ? XY_LEGEND_HEIGHT : 0.0);
    double bottom = XY_PADDING + XY_X_LABEL_HEIGHT + (has_x_title ? XY_AXIS_TITLE_PAD : 0.0);
    double left = XY_PADDING + max_y_label_w + XY_Y_LABEL_GAP + (has_y_title ? XY_AXIS_TITLE_PAD : 0.0);
    double right = XY_PADDING;

    double plot_w = XY_PLOT_WIDTH, plot_h = XY_PLOT_HEIGHT;
    double total_w = left + plot_w + right;
    double total_h = top + plot_h + bottom;

    size_t data_count = nixie_xy_get_data_count(chart);
    double band_width = plot_w / (double)data_count;

    /* xScale(i) = left + (i+0.5) * bandWidth; yScale(v) maps [y_min,y_max] to [top+plot_h, top] */
    double range_span = (y_max - y_min) != 0.0 ? (y_max - y_min) : 1.0;

    nixie_positioned_xy_chart_t *pc = (nixie_positioned_xy_chart_t *)nixie_arena_alloc_zeroed(arena, sizeof(*pc));
    pc->width = total_w;
    pc->height = total_h;
    pc->horizontal = 0;
    pc->plot_x = left;
    pc->plot_y = top;
    pc->plot_w = plot_w;
    pc->plot_h = plot_h;

    if (has_title) {
        pc->has_title = 1;
        pc->title_text = chart->title;
        pc->title_x = total_w / 2.0;
        pc->title_y = XY_PADDING + XY_TITLE_FONT_SIZE;
    }

    char **cat_labels = nixie_xy_get_category_labels(arena, chart, data_count);

    /* X-axis ticks (category labels) */
    pc->x_axis.tick_count = data_count;
    pc->x_axis.ticks = (nixie_xy_tick_t *)nixie_arena_alloc(arena, data_count * sizeof(nixie_xy_tick_t));
    for (size_t i = 0; i < data_count; i++) {
        double x = left + ((double)i + 0.5) * band_width;
        double axis_y = top + plot_h;
        pc->x_axis.ticks[i].label = cat_labels[i];
        pc->x_axis.ticks[i].x = x;
        pc->x_axis.ticks[i].y = axis_y;
        pc->x_axis.ticks[i].tx = x;
        pc->x_axis.ticks[i].ty = axis_y + XY_TICK_LENGTH;
        pc->x_axis.ticks[i].label_x = x;
        pc->x_axis.ticks[i].label_y = axis_y + 18.0;
        pc->x_axis.ticks[i].text_anchor = NIXIE_XY_ANCHOR_MIDDLE;
    }
    pc->x_axis.line_x1 = left;
    pc->x_axis.line_y1 = top + plot_h;
    pc->x_axis.line_x2 = left + plot_w;
    pc->x_axis.line_y2 = top + plot_h;
    if (has_x_title) {
        pc->x_axis.has_title = 1;
        pc->x_axis.title_text = chart->x_axis.title;
        pc->x_axis.title_x = left + plot_w / 2.0;
        pc->x_axis.title_y = total_h - XY_PADDING;
    }

    /* Y-axis ticks + grid lines */
    pc->y_axis.tick_count = y_tick_count;
    pc->y_axis.ticks = (nixie_xy_tick_t *)nixie_arena_alloc(arena, y_tick_count * sizeof(nixie_xy_tick_t));
    pc->grid_line_count = y_tick_count;
    pc->grid_lines = (nixie_xy_grid_line_t *)nixie_arena_alloc(arena, y_tick_count * sizeof(nixie_xy_grid_line_t));
    for (size_t i = 0; i < y_tick_count; i++) {
        double t = (y_ticks[i] - y_min) / range_span;
        double y = top + plot_h - t * plot_h;
        char buf[64];
        nixie_xy_format_tick_value(y_ticks[i], buf, sizeof(buf));

        pc->y_axis.ticks[i].label = nixie_arena_strdup(arena, buf);
        pc->y_axis.ticks[i].x = left;
        pc->y_axis.ticks[i].y = y;
        pc->y_axis.ticks[i].tx = left - XY_TICK_LENGTH;
        pc->y_axis.ticks[i].ty = y;
        pc->y_axis.ticks[i].label_x = left - XY_Y_LABEL_GAP;
        pc->y_axis.ticks[i].label_y = y;
        pc->y_axis.ticks[i].text_anchor = NIXIE_XY_ANCHOR_END;

        pc->grid_lines[i].x1 = left;
        pc->grid_lines[i].y1 = y;
        pc->grid_lines[i].x2 = left + plot_w;
        pc->grid_lines[i].y2 = y;
    }
    pc->y_axis.line_x1 = left;
    pc->y_axis.line_y1 = top;
    pc->y_axis.line_x2 = left;
    pc->y_axis.line_y2 = top + plot_h;
    if (has_y_title) {
        pc->y_axis.has_title = 1;
        pc->y_axis.title_text = chart->y_axis.title;
        pc->y_axis.title_x = XY_PADDING + 4.0;
        pc->y_axis.title_y = top + plot_h / 2.0;
        pc->y_axis.title_rotate = -90.0;
    }

    /* Bars */
    size_t bar_series_count = count_series_of_type(chart, NIXIE_XY_BAR);
    pc->bar_count = count_total_bars(chart);
    pc->bars = pc->bar_count > 0 ? (nixie_positioned_bar_t *)nixie_arena_alloc(arena, pc->bar_count * sizeof(nixie_positioned_bar_t)) : NULL;
    if (bar_series_count > 0) {
        double usable = band_width * (1.0 - XY_BAR_PAD_RATIO);
        double raw_bar_w = bar_series_count > 1 ? (usable - (double)(bar_series_count - 1) * XY_BAR_GROUP_GAP) / (double)bar_series_count : usable;
        double single_bar_w = raw_bar_w < XY_MAX_BAR_WIDTH ? raw_bar_w : XY_MAX_BAR_WIDTH;
        double group_w = bar_series_count > 1 ? single_bar_w * (double)bar_series_count + XY_BAR_GROUP_GAP * (double)(bar_series_count - 1) : single_bar_w;

        double base_y_val = 0.0 > y_min ? 0.0 : y_min;
        double base_y = top + plot_h - ((base_y_val - y_min) / range_span) * plot_h;

        size_t out_idx = 0;
        int b_idx = 0;
        for (size_t si = 0; si < chart->series_count; si++) {
            if (chart->series[si].type != NIXIE_XY_BAR) continue;
            const nixie_xy_series_t *s = &chart->series[si];
            for (size_t i = 0; i < s->data_count; i++) {
                double cx = left + ((double)i + 0.5) * band_width;
                double group_left = cx - group_w / 2.0;
                double bx = group_left + (double)b_idx * (single_bar_w + XY_BAR_GROUP_GAP);
                double val_y = top + plot_h - ((s->data[i] - y_min) / range_span) * plot_h;

                pc->bars[out_idx].x = bx;
                pc->bars[out_idx].y = val_y < base_y ? val_y : base_y;
                pc->bars[out_idx].w = single_bar_w;
                pc->bars[out_idx].h = fabs(base_y - val_y);
                pc->bars[out_idx].value = s->data[i];
                pc->bars[out_idx].label = cat_labels[i];
                pc->bars[out_idx].series_index = b_idx;
                pc->bars[out_idx].color_index = (int)si;
                out_idx++;
            }
            b_idx++;
        }
    }

    /* Lines */
    pc->line_count = count_series_of_type(chart, NIXIE_XY_LINE);
    pc->lines = pc->line_count > 0 ? (nixie_positioned_line_t *)nixie_arena_alloc(arena, pc->line_count * sizeof(nixie_positioned_line_t)) : NULL;
    {
        size_t out_idx = 0;
        int l_idx = 0;
        for (size_t si = 0; si < chart->series_count; si++) {
            if (chart->series[si].type != NIXIE_XY_LINE) continue;
            const nixie_xy_series_t *s = &chart->series[si];
            nixie_xy_line_point_t *points =
                (nixie_xy_line_point_t *)nixie_arena_alloc(arena, s->data_count * sizeof(nixie_xy_line_point_t));
            for (size_t i = 0; i < s->data_count; i++) {
                points[i].x = left + ((double)i + 0.5) * band_width;
                points[i].y = top + plot_h - ((s->data[i] - y_min) / range_span) * plot_h;
                points[i].value = s->data[i];
                points[i].label = cat_labels[i];
            }
            pc->lines[out_idx].points = points;
            pc->lines[out_idx].point_count = s->data_count;
            pc->lines[out_idx].series_index = l_idx;
            pc->lines[out_idx].color_index = (int)si;
            out_idx++;
            l_idx++;
        }
    }

    /* Legend */
    if (has_legend) {
        double legend_y = XY_PADDING + (has_title ? XY_TITLE_HEIGHT : 0.0) + XY_LEGEND_HEIGHT / 2.0;
        pc->legend = build_legend_items(arena, chart, total_w / 2.0, legend_y, &pc->legend_count);
    }

    return pc;
}

/* ==========================================================================
 * Horizontal layout
 * ========================================================================== */

static nixie_positioned_xy_chart_t *layout_horizontal(nixie_arena_t *arena, const nixie_xy_chart_t *chart) {
    int has_title = chart->title != NULL;
    int has_x_title = chart->x_axis.title != NULL;
    int has_y_title = chart->y_axis.title != NULL;
    int has_legend = chart->series_count > 1;

    double y_min = chart->y_axis.range_min, y_max = chart->y_axis.range_max;
    size_t value_tick_count;
    double *value_ticks = nixie_xy_nice_tick_values(arena, y_min, y_max, &value_tick_count);

    size_t data_count = nixie_xy_get_data_count(chart);
    char **cat_labels = nixie_xy_get_category_labels(arena, chart, data_count);

    double max_cat_label_w = 40.0;
    for (size_t i = 0; i < data_count; i++) {
        double w = estimate_label_width(cat_labels[i]);
        if (w > max_cat_label_w) max_cat_label_w = w;
    }

    double top = XY_PADDING + (has_title ? XY_TITLE_HEIGHT : 0.0) + (has_legend ? XY_LEGEND_HEIGHT : 0.0);
    double bottom = XY_PADDING + XY_X_LABEL_HEIGHT + (has_y_title ? XY_AXIS_TITLE_PAD : 0.0);
    double left = XY_PADDING + max_cat_label_w + XY_Y_LABEL_GAP + (has_x_title ? XY_AXIS_TITLE_PAD : 0.0);
    double right = XY_PADDING;

    double plot_w = XY_PLOT_WIDTH, plot_h = XY_PLOT_HEIGHT;
    double total_w = left + plot_w + right;
    double total_h = top + plot_h + bottom;

    double range_span = (y_max - y_min) != 0.0 ? (y_max - y_min) : 1.0;
    double band_height = plot_h / (double)data_count;

    nixie_positioned_xy_chart_t *pc = (nixie_positioned_xy_chart_t *)nixie_arena_alloc_zeroed(arena, sizeof(*pc));
    pc->width = total_w;
    pc->height = total_h;
    pc->horizontal = 1;
    pc->plot_x = left;
    pc->plot_y = top;
    pc->plot_w = plot_w;
    pc->plot_h = plot_h;

    if (has_title) {
        pc->has_title = 1;
        pc->title_text = chart->title;
        pc->title_x = total_w / 2.0;
        pc->title_y = XY_PADDING + XY_TITLE_FONT_SIZE;
    }

    /* X-axis (bottom): value ticks. Y-axis (left): category ticks. */
    pc->x_axis.tick_count = value_tick_count;
    pc->x_axis.ticks = (nixie_xy_tick_t *)nixie_arena_alloc(arena, value_tick_count * sizeof(nixie_xy_tick_t));
    pc->grid_line_count = value_tick_count;
    pc->grid_lines = (nixie_xy_grid_line_t *)nixie_arena_alloc(arena, value_tick_count * sizeof(nixie_xy_grid_line_t));
    for (size_t i = 0; i < value_tick_count; i++) {
        double t = (value_ticks[i] - y_min) / range_span;
        double x = left + t * plot_w;
        char buf[64];
        nixie_xy_format_tick_value(value_ticks[i], buf, sizeof(buf));

        pc->x_axis.ticks[i].label = nixie_arena_strdup(arena, buf);
        pc->x_axis.ticks[i].x = x;
        pc->x_axis.ticks[i].y = top + plot_h;
        pc->x_axis.ticks[i].tx = x;
        pc->x_axis.ticks[i].ty = top + plot_h + XY_TICK_LENGTH;
        pc->x_axis.ticks[i].label_x = x;
        pc->x_axis.ticks[i].label_y = top + plot_h + 18.0;
        pc->x_axis.ticks[i].text_anchor = NIXIE_XY_ANCHOR_MIDDLE;

        pc->grid_lines[i].x1 = x;
        pc->grid_lines[i].y1 = top;
        pc->grid_lines[i].x2 = x;
        pc->grid_lines[i].y2 = top + plot_h;
    }
    pc->x_axis.line_x1 = left;
    pc->x_axis.line_y1 = top + plot_h;
    pc->x_axis.line_x2 = left + plot_w;
    pc->x_axis.line_y2 = top + plot_h;
    if (has_y_title) {
        /* In horizontal mode the "y-axis" title describes values (bottom axis). */
        pc->x_axis.has_title = 1;
        pc->x_axis.title_text = chart->y_axis.title;
        pc->x_axis.title_x = left + plot_w / 2.0;
        pc->x_axis.title_y = total_h - XY_PADDING;
    }

    pc->y_axis.tick_count = data_count;
    pc->y_axis.ticks = (nixie_xy_tick_t *)nixie_arena_alloc(arena, data_count * sizeof(nixie_xy_tick_t));
    for (size_t i = 0; i < data_count; i++) {
        double y = top + ((double)i + 0.5) * band_height;
        pc->y_axis.ticks[i].label = cat_labels[i];
        pc->y_axis.ticks[i].x = left;
        pc->y_axis.ticks[i].y = y;
        pc->y_axis.ticks[i].tx = left - XY_TICK_LENGTH;
        pc->y_axis.ticks[i].ty = y;
        pc->y_axis.ticks[i].label_x = left - XY_Y_LABEL_GAP;
        pc->y_axis.ticks[i].label_y = y;
        pc->y_axis.ticks[i].text_anchor = NIXIE_XY_ANCHOR_END;
    }
    pc->y_axis.line_x1 = left;
    pc->y_axis.line_y1 = top;
    pc->y_axis.line_x2 = left;
    pc->y_axis.line_y2 = top + plot_h;
    if (has_x_title) {
        /* In horizontal mode the "x-axis" title describes categories (left axis). */
        pc->y_axis.has_title = 1;
        pc->y_axis.title_text = chart->x_axis.title;
        pc->y_axis.title_x = XY_PADDING + 4.0;
        pc->y_axis.title_y = top + plot_h / 2.0;
        pc->y_axis.title_rotate = -90.0;
    }

    /* Bars (horizontal) */
    size_t bar_series_count = count_series_of_type(chart, NIXIE_XY_BAR);
    pc->bar_count = count_total_bars(chart);
    pc->bars = pc->bar_count > 0 ? (nixie_positioned_bar_t *)nixie_arena_alloc(arena, pc->bar_count * sizeof(nixie_positioned_bar_t)) : NULL;
    if (bar_series_count > 0) {
        double usable = band_height * (1.0 - XY_BAR_PAD_RATIO);
        double raw_bar_h = bar_series_count > 1 ? (usable - (double)(bar_series_count - 1) * XY_BAR_GROUP_GAP) / (double)bar_series_count : usable;
        double single_bar_h = raw_bar_h < XY_MAX_BAR_WIDTH ? raw_bar_h : XY_MAX_BAR_WIDTH;

        double base_val = 0.0 > y_min ? 0.0 : y_min;
        double base_x = left + ((base_val - y_min) / range_span) * plot_w;

        size_t out_idx = 0;
        int b_idx = 0;
        for (size_t si = 0; si < chart->series_count; si++) {
            if (chart->series[si].type != NIXIE_XY_BAR) continue;
            const nixie_xy_series_t *s = &chart->series[si];
            for (size_t i = 0; i < s->data_count; i++) {
                double cy = top + ((double)i + 0.5) * band_height;
                double group_h = bar_series_count > 1
                                     ? single_bar_h * (double)bar_series_count + XY_BAR_GROUP_GAP * (double)(bar_series_count - 1)
                                     : single_bar_h;
                double group_top = cy - group_h / 2.0;
                double by = group_top + (double)b_idx * (single_bar_h + XY_BAR_GROUP_GAP);
                double val_x = left + ((s->data[i] - y_min) / range_span) * plot_w;

                pc->bars[out_idx].x = val_x < base_x ? val_x : base_x;
                pc->bars[out_idx].y = by;
                pc->bars[out_idx].w = fabs(val_x - base_x);
                pc->bars[out_idx].h = single_bar_h;
                pc->bars[out_idx].value = s->data[i];
                pc->bars[out_idx].label = cat_labels[i];
                pc->bars[out_idx].series_index = b_idx;
                pc->bars[out_idx].color_index = (int)si;
                out_idx++;
            }
            b_idx++;
        }
    }

    /* Lines (horizontal: value on x, category index on y) */
    pc->line_count = count_series_of_type(chart, NIXIE_XY_LINE);
    pc->lines = pc->line_count > 0 ? (nixie_positioned_line_t *)nixie_arena_alloc(arena, pc->line_count * sizeof(nixie_positioned_line_t)) : NULL;
    {
        size_t out_idx = 0;
        int l_idx = 0;
        for (size_t si = 0; si < chart->series_count; si++) {
            if (chart->series[si].type != NIXIE_XY_LINE) continue;
            const nixie_xy_series_t *s = &chart->series[si];
            nixie_xy_line_point_t *points =
                (nixie_xy_line_point_t *)nixie_arena_alloc(arena, s->data_count * sizeof(nixie_xy_line_point_t));
            for (size_t i = 0; i < s->data_count; i++) {
                points[i].x = left + ((s->data[i] - y_min) / range_span) * plot_w;
                points[i].y = top + ((double)i + 0.5) * band_height;
                points[i].value = s->data[i];
                points[i].label = cat_labels[i];
            }
            pc->lines[out_idx].points = points;
            pc->lines[out_idx].point_count = s->data_count;
            pc->lines[out_idx].series_index = l_idx;
            pc->lines[out_idx].color_index = (int)si;
            out_idx++;
            l_idx++;
        }
    }

    if (has_legend) {
        double legend_y = XY_PADDING + (has_title ? XY_TITLE_HEIGHT : 0.0) + XY_LEGEND_HEIGHT / 2.0;
        pc->legend = build_legend_items(arena, chart, total_w / 2.0, legend_y, &pc->legend_count);
    }

    return pc;
}

nixie_positioned_xy_chart_t *nixie_xy_layout(nixie_arena_t *arena, const nixie_xy_chart_t *chart) {
    if (chart->horizontal) return layout_horizontal(arena, chart);
    return layout_vertical(arena, chart);
}
