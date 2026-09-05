#ifndef NIXIE_XYCHART_MODEL_H
#define NIXIE_XYCHART_MODEL_H

#include <stddef.h>

typedef enum nixie_xy_series_type {
    NIXIE_XY_BAR,
    NIXIE_XY_LINE
} nixie_xy_series_type_t;

/* Categorical (categories != NULL) and numeric-range (has_range) forms are
 * mutually exclusive, mirroring beautiful-mermaid/src/xychart/types.ts's
 * XYAxis (categories? vs range?). */
typedef struct nixie_xy_axis {
    char *title; /* nullable */

    char **categories;
    size_t category_count;

    int has_range;
    double range_min;
    double range_max;
} nixie_xy_axis_t;

typedef struct nixie_xy_series {
    nixie_xy_series_type_t type;
    double *data;
    size_t data_count;
} nixie_xy_series_t;

typedef struct nixie_xy_chart {
    char *title; /* nullable */
    int horizontal;
    nixie_xy_axis_t x_axis;
    nixie_xy_axis_t y_axis;

    nixie_xy_series_t *series;
    size_t series_count;
    size_t series_cap;
} nixie_xy_chart_t;

#endif /* NIXIE_XYCHART_MODEL_H */
