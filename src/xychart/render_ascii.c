#include "render_ascii.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../strbuf.h"
#include "../text_metrics.h"
#include "layout.h"

#define PLOT_WIDTH 60
#define PLOT_HEIGHT 20

/* ==========================================================================
 * Character grid (small local copy -- see flowchart/render_ascii.c's note
 * on why these aren't factored into a shared module)
 * ========================================================================== */

typedef struct nixie_ascii_grid {
    uint32_t *cells;
    int width;
    int height;
} nixie_ascii_grid_t;

static nixie_ascii_grid_t *grid_create(nixie_arena_t *a, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    nixie_ascii_grid_t *g = (nixie_ascii_grid_t *)nixie_arena_alloc(a, sizeof(nixie_ascii_grid_t));
    g->width = w;
    g->height = h;
    g->cells = (uint32_t *)nixie_arena_alloc(a, (size_t)w * (size_t)h * sizeof(uint32_t));
    for (int i = 0; i < w * h; i++) g->cells[i] = ' ';
    return g;
}

static void grid_set(nixie_ascii_grid_t *g, int row, int col, uint32_t ch) {
    if (col < 0 || row < 0 || col >= g->width || row >= g->height) return;
    g->cells[(size_t)row * (size_t)g->width + (size_t)col] = ch;
}

static uint32_t grid_get(const nixie_ascii_grid_t *g, int row, int col) {
    if (col < 0 || row < 0 || col >= g->width || row >= g->height) return ' ';
    return g->cells[(size_t)row * (size_t)g->width + (size_t)col];
}

static void append_utf8(nixie_strbuf_t *sb, uint32_t cp) {
    char buf[4];
    if (cp < 0x80) {
        nixie_strbuf_append_char(sb, (char)cp);
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        nixie_strbuf_append_n(sb, buf, 2);
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        nixie_strbuf_append_n(sb, buf, 3);
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        nixie_strbuf_append_n(sb, buf, 4);
    }
}

static void grid_write(nixie_strbuf_t *sb, const nixie_ascii_grid_t *g) {
    int last_non_blank_row = -1;
    for (int row = 0; row < g->height; row++) {
        for (int col = 0; col < g->width; col++) {
            if (grid_get(g, row, col) != (uint32_t)' ') { last_non_blank_row = row; break; }
        }
    }
    for (int row = 0; row <= last_non_blank_row; row++) {
        int last_non_space = -1;
        for (int col = 0; col < g->width; col++) {
            if (grid_get(g, row, col) != (uint32_t)' ') last_non_space = col;
        }
        for (int col = 0; col <= last_non_space; col++) {
            append_utf8(sb, grid_get(g, row, col));
        }
        if (row < last_non_blank_row) nixie_strbuf_append_char(sb, '\n');
    }
}

static void write_text(nixie_ascii_grid_t *g, int row, int col, const char *text) {
    const char *q = text;
    while (*q != '\0') {
        uint32_t cp;
        q += nixie_utf8_decode(q, &cp);
        grid_set(g, row, col, cp);
        col++;
    }
}

/* ==========================================================================
 * Glyph sets
 * ========================================================================== */

typedef struct {
    uint32_t h_line, v_line, origin, y_tick, x_tick, bar, grid, corner_tl, corner_tr, corner_bl, corner_br;
} xy_glyphs_t;

static xy_glyphs_t glyphs_for(int use_unicode) {
    xy_glyphs_t g;
    if (use_unicode) {
        g.h_line = 0x2500; g.v_line = 0x2502; g.origin = 0x253C; g.y_tick = 0x2524; g.x_tick = 0x252C;
        g.bar = 0x2588; g.grid = 0xB7;
        g.corner_tl = 0x256D; g.corner_tr = 0x256E; g.corner_bl = 0x2570; g.corner_br = 0x256F;
    } else {
        g.h_line = '-'; g.v_line = '|'; g.origin = '+'; g.y_tick = '+'; g.x_tick = '+';
        g.bar = '#'; g.grid = '.';
        g.corner_tl = '+'; g.corner_tr = '+'; g.corner_bl = '+'; g.corner_br = '+';
    }
    return g;
}

/* ==========================================================================
 * Legend
 * ========================================================================== */

static void draw_legend(nixie_ascii_grid_t *g, const nixie_xy_chart_t *chart, int row, int total_w, const xy_glyphs_t *ch) {
    size_t n = chart->series_count;
    int bar_idx = 0, line_idx = 0;
    int total_len = 0;

    for (size_t si = 0; si < n; si++) {
        char label[32];
        if (chart->series[si].type == NIXIE_XY_BAR) {
            snprintf(label, sizeof(label), "Bar %d", ++bar_idx);
        } else {
            snprintf(label, sizeof(label), "Line %d", ++line_idx);
        }
        if (si > 0) total_len += 2;
        total_len += 1 + 1 + (int)strlen(label);
    }

    int col = total_w / 2 - total_len / 2;
    if (col < 0) col = 0;

    bar_idx = 0;
    line_idx = 0;
    for (size_t si = 0; si < n; si++) {
        if (si > 0) col += 2;
        char label[32];
        uint32_t symbol;
        if (chart->series[si].type == NIXIE_XY_BAR) {
            snprintf(label, sizeof(label), "Bar %d", ++bar_idx);
            symbol = ch->bar;
        } else {
            snprintf(label, sizeof(label), "Line %d", ++line_idx);
            symbol = ch->h_line;
        }
        grid_set(g, row, col, symbol);
        col += 2;
        write_text(g, row, col, label);
        col += (int)strlen(label);
    }
}

/* ==========================================================================
 * Staircase line drawing
 * ========================================================================== */

static void draw_staircase_line_vertical(
    nixie_ascii_grid_t *g, const double *data, size_t data_count, int (*band_center)(int, void *), void *bc_ctx,
    int (*value_to_row)(double, void *), void *vr_ctx, int plot_top, int plot_h, int plot_left, int plot_total_w,
    const xy_glyphs_t *ch) {
    if (data_count == 0) return;

    typedef struct { int col, row; } pt_t;
    pt_t *points = (pt_t *)malloc(data_count * sizeof(pt_t));
    for (size_t i = 0; i < data_count; i++) {
        points[i].col = band_center((int)i, bc_ctx);
        points[i].row = value_to_row(data[i], vr_ctx);
    }

    if (data_count == 1) {
        int display_row = plot_top + (plot_h - 1 - points[0].row);
        grid_set(g, display_row, points[0].col, ch->h_line);
        free(points);
        return;
    }

    for (size_t i = 0; i + 1 < data_count; i++) {
        pt_t p1 = points[i], p2 = points[i + 1];

        if (p1.row == p2.row) {
            for (int c = p1.col; c <= p2.col; c++) {
                grid_set(g, plot_top + (plot_h - 1 - p1.row), c, ch->h_line);
            }
            continue;
        }

        int mid_col = (p1.col + p2.col) / 2;
        int going_up = p2.row > p1.row;

        for (int c = p1.col; c < mid_col; c++) grid_set(g, plot_top + (plot_h - 1 - p1.row), c, ch->h_line);
        grid_set(g, plot_top + (plot_h - 1 - p1.row), mid_col, going_up ? ch->corner_br : ch->corner_tr);

        int min_row = p1.row < p2.row ? p1.row : p2.row;
        int max_row = p1.row > p2.row ? p1.row : p2.row;
        for (int row = min_row + 1; row < max_row; row++) {
            grid_set(g, plot_top + (plot_h - 1 - row), mid_col, ch->v_line);
        }

        grid_set(g, plot_top + (plot_h - 1 - p2.row), mid_col, going_up ? ch->corner_tl : ch->corner_bl);
        for (int c = mid_col + 1; c <= p2.col; c++) grid_set(g, plot_top + (plot_h - 1 - p2.row), c, ch->h_line);

        if (i == 0) {
            int lead_start = p1.col - (p2.col - p1.col) / 4;
            if (lead_start < plot_left) lead_start = plot_left;
            for (int c = lead_start; c < p1.col; c++) grid_set(g, plot_top + (plot_h - 1 - p1.row), c, ch->h_line);
        }
        if (i + 2 == data_count) {
            int trail_end = p2.col + (p2.col - p1.col) / 4;
            if (trail_end > plot_left + plot_total_w - 1) trail_end = plot_left + plot_total_w - 1;
            for (int c = p2.col + 1; c <= trail_end; c++) grid_set(g, plot_top + (plot_h - 1 - p2.row), c, ch->h_line);
        }
    }

    free(points);
}

static void draw_staircase_line_horizontal(
    nixie_ascii_grid_t *g, const double *data, size_t data_count, int (*band_mid)(int, void *), void *bm_ctx,
    int (*value_to_col)(double, void *), void *vc_ctx, const xy_glyphs_t *ch) {
    if (data_count == 0) return;

    typedef struct { int row, col; } pt_t;
    pt_t *points = (pt_t *)malloc(data_count * sizeof(pt_t));
    for (size_t i = 0; i < data_count; i++) {
        points[i].row = band_mid((int)i, bm_ctx);
        points[i].col = value_to_col(data[i], vc_ctx);
    }

    if (data_count == 1) {
        grid_set(g, points[0].row, points[0].col, ch->v_line);
        free(points);
        return;
    }

    for (size_t i = 0; i + 1 < data_count; i++) {
        pt_t p1 = points[i], p2 = points[i + 1];

        if (p1.col == p2.col) {
            for (int r = p1.row; r <= p2.row; r++) grid_set(g, r, p1.col, ch->v_line);
            continue;
        }

        int mid_row = (p1.row + p2.row) / 2;
        int going_right = p2.col > p1.col;

        for (int r = p1.row; r < mid_row; r++) grid_set(g, r, p1.col, ch->v_line);
        grid_set(g, mid_row, p1.col, going_right ? ch->corner_bl : ch->corner_br);

        int min_col = p1.col < p2.col ? p1.col : p2.col;
        int max_col = p1.col > p2.col ? p1.col : p2.col;
        for (int c = min_col + 1; c < max_col; c++) grid_set(g, mid_row, c, ch->h_line);

        grid_set(g, mid_row, p2.col, going_right ? ch->corner_tr : ch->corner_tl);
        for (int r = mid_row + 1; r <= p2.row; r++) grid_set(g, r, p2.col, ch->v_line);
    }

    free(points);
}

/* ==========================================================================
 * Vertical chart
 * ========================================================================== */

typedef struct { int plot_left, band_w; } band_center_ctx_t;
static int band_center_fn(int i, void *ctx) {
    band_center_ctx_t *c = (band_center_ctx_t *)ctx;
    return c->plot_left + (int)((double)c->band_w * ((double)i + 0.5));
}

typedef struct { double range_min, range_span; int plot_h; } value_to_row_ctx_t;
static int value_to_row_fn(double v, void *ctx) {
    value_to_row_ctx_t *c = (value_to_row_ctx_t *)ctx;
    double t = (v - c->range_min) / c->range_span;
    return (int)(t * (double)(c->plot_h - 1) + (t >= 0 ? 0.5 : -0.5));
}

static char *render_vertical(nixie_arena_t *arena, const nixie_xy_chart_t *chart, const xy_glyphs_t *ch) {
    size_t data_count = nixie_xy_get_data_count(chart);

    double y_min = chart->y_axis.range_min, y_max = chart->y_axis.range_max;
    double range_span = (y_max - y_min) != 0.0 ? (y_max - y_min) : 1.0;
    size_t y_tick_count;
    double *y_ticks = nixie_xy_nice_tick_values(arena, y_min, y_max, &y_tick_count);

    size_t y_gutter = 1;
    for (size_t i = 0; i < y_tick_count; i++) {
        char buf[64];
        nixie_xy_format_tick_value(y_ticks[i], buf, sizeof(buf));
        size_t l = strlen(buf);
        if (l + 1 > y_gutter) y_gutter = l + 1;
    }

    int plot_w = PLOT_WIDTH;
    if ((int)(data_count * 6) > plot_w) plot_w = (int)(data_count * 6);
    int plot_h = PLOT_HEIGHT;
    int band_w = plot_w / (int)data_count;
    if (band_w < 1) band_w = 1;
    char **cat_labels = nixie_xy_get_category_labels(arena, chart, data_count);

    int has_title = chart->title != NULL;
    int has_x_title = chart->x_axis.title != NULL;
    int has_legend = chart->series_count > 1;

    int plot_top = (has_title ? 2 : 0) + (has_legend ? 1 : 0);
    int plot_left = (int)y_gutter + 1;
    int total_w = plot_left + band_w * (int)data_count + 2;
    int x_axis_row = plot_top + plot_h;
    int x_label_row = x_axis_row + 1;
    int x_title_row = has_x_title ? x_label_row + 1 : -1;
    int total_h = x_label_row + 1 + (has_x_title ? 1 : 0);

    nixie_ascii_grid_t *g = grid_create(arena, total_w, total_h);

    band_center_ctx_t bc_ctx = {plot_left, band_w};
    value_to_row_ctx_t vr_ctx = {y_min, range_span, plot_h};

    if (has_title) {
        int col = total_w / 2 - (int)strlen(chart->title) / 2;
        write_text(g, 0, col < 0 ? 0 : col, chart->title);
    }
    if (has_legend) {
        draw_legend(g, chart, has_title ? 1 : 0, total_w, ch);
    }

    for (int row = 0; row < plot_h; row++) {
        int display_row = plot_top + (plot_h - 1 - row);
        grid_set(g, display_row, plot_left - 1, ch->v_line);
    }
    grid_set(g, x_axis_row, plot_left - 1, ch->origin);

    for (size_t i = 0; i < y_tick_count; i++) {
        int row = value_to_row_fn(y_ticks[i], &vr_ctx);
        if (row < 0 || row >= plot_h) continue;
        int display_row = plot_top + (plot_h - 1 - row);
        char buf[64];
        nixie_xy_format_tick_value(y_ticks[i], buf, sizeof(buf));
        grid_set(g, display_row, plot_left - 1, row == 0 ? ch->origin : ch->y_tick);
        int label_start = (int)y_gutter - (int)strlen(buf);
        write_text(g, display_row, label_start < 0 ? 0 : label_start, buf);
    }

    for (int c = plot_left; c < plot_left + band_w * (int)data_count; c++) {
        grid_set(g, x_axis_row, c, ch->h_line);
    }
    for (size_t i = 0; i < data_count; i++) {
        int cx = band_center_fn((int)i, &bc_ctx);
        grid_set(g, x_axis_row, cx, ch->x_tick);
        int label_start = cx - (int)strlen(cat_labels[i]) / 2;
        write_text(g, x_label_row, label_start < 0 ? 0 : label_start, cat_labels[i]);
    }
    if (has_x_title) {
        int col = total_w / 2 - (int)strlen(chart->x_axis.title) / 2;
        write_text(g, x_title_row, col < 0 ? 0 : col, chart->x_axis.title);
    }

    for (size_t i = 0; i < y_tick_count; i++) {
        int row = value_to_row_fn(y_ticks[i], &vr_ctx);
        if (row < 0 || row >= plot_h) continue;
        int display_row = plot_top + (plot_h - 1 - row);
        for (int c = plot_left; c < plot_left + band_w * (int)data_count; c++) {
            if (grid_get(g, display_row, c) == (uint32_t)' ') grid_set(g, display_row, c, ch->grid);
        }
    }

    size_t bar_series = 0;
    for (size_t si = 0; si < chart->series_count; si++) {
        if (chart->series[si].type == NIXIE_XY_BAR) bar_series++;
    }
    if (bar_series > 0) {
        int usable = band_w - 2;
        if (usable < 1) usable = 1;
        int single_bar_w = usable / (int)bar_series;
        if (single_bar_w < 1) single_bar_w = 1;
        if (single_bar_w > 8) single_bar_w = 8;
        int group_w = single_bar_w * (int)bar_series + ((int)bar_series - 1);
        double base_val = 0.0 > y_min ? 0.0 : y_min;
        int base_row = value_to_row_fn(base_val, &vr_ctx);

        int b_idx = 0;
        for (size_t si = 0; si < chart->series_count; si++) {
            if (chart->series[si].type != NIXIE_XY_BAR) continue;
            const nixie_xy_series_t *s = &chart->series[si];
            for (size_t i = 0; i < s->data_count; i++) {
                int cx = band_center_fn((int)i, &bc_ctx);
                int group_left = cx - group_w / 2;
                int bx = group_left + b_idx * (single_bar_w + 1);
                int val_row = value_to_row_fn(s->data[i], &vr_ctx);
                int from_row = base_row < val_row ? base_row : val_row;
                int to_row = base_row > val_row ? base_row : val_row;
                for (int row = from_row; row <= to_row; row++) {
                    int display_row = plot_top + (plot_h - 1 - row);
                    for (int c = bx; c < bx + single_bar_w; c++) grid_set(g, display_row, c, ch->bar);
                }
            }
            b_idx++;
        }
    }

    for (size_t si = 0; si < chart->series_count; si++) {
        if (chart->series[si].type != NIXIE_XY_LINE) continue;
        const nixie_xy_series_t *s = &chart->series[si];
        draw_staircase_line_vertical(
            g, s->data, s->data_count, band_center_fn, &bc_ctx, value_to_row_fn, &vr_ctx, plot_top, plot_h, plot_left,
            band_w * (int)data_count, ch);
    }

    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);
    grid_write(&sb, g);
    return nixie_strbuf_release(&sb);
}

/* ==========================================================================
 * Horizontal chart
 * ========================================================================== */

typedef struct { int plot_top, band_h; } band_mid_ctx_t;
static int band_mid_fn(int i, void *ctx) {
    band_mid_ctx_t *c = (band_mid_ctx_t *)ctx;
    return c->plot_top + (int)((double)c->band_h * ((double)i + 0.5));
}

typedef struct { double range_min, range_span; int plot_left, plot_w; } value_to_col_ctx_t;
static int value_to_col_fn(double v, void *ctx) {
    value_to_col_ctx_t *c = (value_to_col_ctx_t *)ctx;
    double t = (v - c->range_min) / c->range_span;
    return c->plot_left + (int)(t * (double)(c->plot_w - 1) + (t >= 0 ? 0.5 : -0.5));
}

static char *render_horizontal(nixie_arena_t *arena, const nixie_xy_chart_t *chart, const xy_glyphs_t *ch) {
    size_t data_count = nixie_xy_get_data_count(chart);

    double y_min = chart->y_axis.range_min, y_max = chart->y_axis.range_max;
    double range_span = (y_max - y_min) != 0.0 ? (y_max - y_min) : 1.0;
    size_t value_tick_count;
    double *value_ticks = nixie_xy_nice_tick_values(arena, y_min, y_max, &value_tick_count);

    char **cat_labels = nixie_xy_get_category_labels(arena, chart, data_count);
    size_t cat_gutter = 1;
    for (size_t i = 0; i < data_count; i++) {
        size_t l = strlen(cat_labels[i]);
        if (l + 1 > cat_gutter) cat_gutter = l + 1;
    }

    int plot_w = PLOT_WIDTH < 40 ? 40 : PLOT_WIDTH;
    int band_h = (int)(PLOT_HEIGHT / (int)data_count);
    if (band_h < 2) band_h = 2;
    int plot_h = band_h * (int)data_count;

    int has_title = chart->title != NULL;
    int has_y_title = chart->y_axis.title != NULL;
    int has_legend = chart->series_count > 1;

    int plot_top = (has_title ? 2 : 0) + (has_legend ? 1 : 0);
    int plot_left = (int)cat_gutter + 1;
    int total_w = plot_left + plot_w + 2;
    int total_h = plot_top + plot_h + 2 + (has_y_title ? 1 : 0);
    int x_axis_row = plot_top + plot_h;

    nixie_ascii_grid_t *g = grid_create(arena, total_w, total_h);

    value_to_col_ctx_t vc_ctx = {y_min, range_span, plot_left, plot_w};
    band_mid_ctx_t bm_ctx = {plot_top, band_h};

    if (has_title) {
        int col = total_w / 2 - (int)strlen(chart->title) / 2;
        write_text(g, 0, col < 0 ? 0 : col, chart->title);
    }
    if (has_legend) {
        draw_legend(g, chart, has_title ? 1 : 0, total_w, ch);
    }

    for (int r = plot_top; r < plot_top + plot_h; r++) grid_set(g, r, plot_left - 1, ch->v_line);
    grid_set(g, x_axis_row, plot_left - 1, ch->origin);

    for (size_t i = 0; i < data_count; i++) {
        int my = band_mid_fn((int)i, &bm_ctx);
        int label_start = (int)cat_gutter - (int)strlen(cat_labels[i]);
        write_text(g, my, label_start < 0 ? 0 : label_start, cat_labels[i]);
    }

    for (int c = plot_left; c < plot_left + plot_w; c++) grid_set(g, x_axis_row, c, ch->h_line);
    for (size_t i = 0; i < value_tick_count; i++) {
        int cx = value_to_col_fn(value_ticks[i], &vc_ctx);
        if (cx < plot_left || cx >= plot_left + plot_w) continue;
        grid_set(g, x_axis_row, cx, ch->x_tick);
        char buf[64];
        nixie_xy_format_tick_value(value_ticks[i], buf, sizeof(buf));
        int label_start = cx - (int)strlen(buf) / 2;
        write_text(g, x_axis_row + 1, label_start < 0 ? 0 : label_start, buf);
    }

    if (has_y_title) {
        int col = total_w / 2 - (int)strlen(chart->y_axis.title) / 2;
        write_text(g, total_h - 1, col < 0 ? 0 : col, chart->y_axis.title);
    }

    for (size_t i = 0; i < value_tick_count; i++) {
        int cx = value_to_col_fn(value_ticks[i], &vc_ctx);
        if (cx < plot_left || cx >= plot_left + plot_w) continue;
        for (int r = plot_top; r < plot_top + plot_h; r++) {
            if (grid_get(g, r, cx) == (uint32_t)' ') grid_set(g, r, cx, ch->grid);
        }
    }

    size_t bar_series = 0;
    for (size_t si = 0; si < chart->series_count; si++) {
        if (chart->series[si].type == NIXIE_XY_BAR) bar_series++;
    }
    if (bar_series > 0) {
        int single_bar_h = 1;
        int group_h = single_bar_h * (int)bar_series + ((int)bar_series - 1);
        double base_val = 0.0 > y_min ? 0.0 : y_min;
        int base_col = value_to_col_fn(base_val, &vc_ctx);

        int b_idx = 0;
        for (size_t si = 0; si < chart->series_count; si++) {
            if (chart->series[si].type != NIXIE_XY_BAR) continue;
            const nixie_xy_series_t *s = &chart->series[si];
            for (size_t i = 0; i < s->data_count; i++) {
                int my = band_mid_fn((int)i, &bm_ctx);
                int group_top = my - group_h / 2;
                int by = group_top + b_idx * (single_bar_h + 1);
                int val_col = value_to_col_fn(s->data[i], &vc_ctx);
                int from_col = base_col < val_col ? base_col : val_col;
                int to_col = base_col > val_col ? base_col : val_col;
                for (int r = by; r < by + single_bar_h; r++) {
                    for (int c = from_col; c <= to_col; c++) grid_set(g, r, c, ch->bar);
                }
            }
            b_idx++;
        }
    }

    for (size_t si = 0; si < chart->series_count; si++) {
        if (chart->series[si].type != NIXIE_XY_LINE) continue;
        const nixie_xy_series_t *s = &chart->series[si];
        draw_staircase_line_horizontal(g, s->data, s->data_count, band_mid_fn, &bm_ctx, value_to_col_fn, &vc_ctx, ch);
    }

    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);
    grid_write(&sb, g);
    return nixie_strbuf_release(&sb);
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

char *nixie_xy_render_ascii(nixie_arena_t *arena, const nixie_xy_chart_t *chart, const nixie_xy_ascii_options_t *opts) {
    static const nixie_xy_ascii_options_t default_opts = {0};
    if (opts == NULL) opts = &default_opts;
    xy_glyphs_t ch = glyphs_for(opts->use_unicode);

    if (chart->series_count == 0 && chart->x_axis.category_count == 0) {
        nixie_strbuf_t sb;
        nixie_strbuf_init(&sb);
        nixie_strbuf_append(&sb, "(empty chart)");
        return nixie_strbuf_release(&sb);
    }

    if (chart->horizontal) {
        return render_horizontal(arena, chart, &ch);
    }
    return render_vertical(arena, chart, &ch);
}
