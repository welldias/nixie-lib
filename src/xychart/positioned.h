#ifndef NIXIE_XYCHART_POSITIONED_H
#define NIXIE_XYCHART_POSITIONED_H

#include <stddef.h>

#include "model.h"

typedef enum nixie_xy_text_anchor {
    NIXIE_XY_ANCHOR_START,
    NIXIE_XY_ANCHOR_MIDDLE,
    NIXIE_XY_ANCHOR_END
} nixie_xy_text_anchor_t;

typedef struct nixie_xy_tick {
    char *label;
    double x, y;   /* tick mark position on the axis line */
    double tx, ty; /* end of the (short, perpendicular) tick mark */
    double label_x, label_y;
    nixie_xy_text_anchor_t text_anchor;
} nixie_xy_tick_t;

typedef struct nixie_positioned_axis {
    int has_title;
    char *title_text;
    double title_x, title_y, title_rotate; /* rotate in degrees, 0 = none */

    nixie_xy_tick_t *ticks;
    size_t tick_count;

    double line_x1, line_y1, line_x2, line_y2;
} nixie_positioned_axis_t;

typedef struct nixie_positioned_bar {
    double x, y, w, h;
    double value;
    char *label; /* nullable */
    int series_index; /* index within its own type (bar), for grouping */
    int color_index;   /* global index across all series */
} nixie_positioned_bar_t;

typedef struct nixie_xy_line_point {
    double x, y;
    double value;
    char *label; /* nullable */
} nixie_xy_line_point_t;

typedef struct nixie_positioned_line {
    nixie_xy_line_point_t *points;
    size_t point_count;
    int series_index;
    int color_index;
} nixie_positioned_line_t;

typedef struct nixie_xy_grid_line {
    double x1, y1, x2, y2;
} nixie_xy_grid_line_t;

typedef struct nixie_xy_legend_item {
    char *label;
    double x, y;
    nixie_xy_series_type_t type;
    int series_index;
    int color_index;
} nixie_xy_legend_item_t;

typedef struct nixie_positioned_xy_chart {
    double width, height;
    int horizontal;

    int has_title;
    char *title_text;
    double title_x, title_y;

    nixie_positioned_axis_t x_axis;
    nixie_positioned_axis_t y_axis;

    double plot_x, plot_y, plot_w, plot_h;

    nixie_positioned_bar_t *bars;
    size_t bar_count;

    nixie_positioned_line_t *lines;
    size_t line_count;

    nixie_xy_grid_line_t *grid_lines;
    size_t grid_line_count;

    nixie_xy_legend_item_t *legend;
    size_t legend_count;
} nixie_positioned_xy_chart_t;

#endif /* NIXIE_XYCHART_POSITIONED_H */
